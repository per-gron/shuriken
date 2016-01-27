// Copyright 2011 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include "getopt.h"
#include <direct.h>
#include <windows.h>
#elif defined(_AIX)
#include "getopt.h"
#include <unistd.h>
#else
#include <getopt.h>
#include <unistd.h>
#endif

#include "build.h"
#include "build_config.h"
#include "build_error.h"
#include "clean.h"
#include "manifest.h"
#include "persistent_file_system.h"
#include "persistent_invocation_log.h"
#include "util.h"
#include "version.h"

namespace shk {
namespace {

struct Tool;

/**
 * Command-line options.
 */
struct Options {
  /**
   * Build file to load.
   */
  const char *input_file;

  /**
   * Directory to change into before running.
   */
  const char *working_dir;

  /**
   * Tool to run rather than building.
   */
  const Tool *tool;
};

/**
 * The Ninja main() loads up a series of data structures; various tools need
 * to poke into these, so store them as fields on an object.
 */
struct NinjaMain {
  NinjaMain(const BuildConfig &config) :
      _config(config) {}

  /**
   * The type of functions that are the entry points to tools (subcommands).
   */
  typedef int (NinjaMain::*ToolFunc)(int, char**);

  /**
   * collectTarget for all command-line arguments, filling in \a targets.
   */
  std::vector<Node *> collectTargetsFromArgs(
      int argc, char *argv[]) throw(BuildError);

  /**
   * Open the deps log: load it, then open for writing.
   * @return false on error.
   */
  bool openDepsLog(bool recompact_only = false);

  /**
   * Ensure the build directory exists, creating it if necessary.
   * @return false on error.
   */
  bool ensureBuildDirExists();

  /**
   * Rebuild the manifest, if necessary.
   * Fills in \a err on error.
   * @return true if the manifest was rebuilt.
   */
  bool rebuildManifest(const char *input_file, std::string *err);

  /**
   * Build the targets listed on the command line.
   * @return an exit code.
   */
  int runBuild(int argc, char **argv);

 private:
  /**
   * Build configuration set from flags (e.g. parallelism).
   */
  const BuildConfig _config;

  /**
   * Loaded state (rules, nodes).
   */
  State _state;

  /**
   * Functions for accesssing the disk.
   */
  RealDiskInterface _disk_interface;

  /**
   * The build directory, used for storing the build log etc.
   */
  std::string _build_dir;

  DepsLog _deps_log;
};

/**
 * Subtools, accessible via "-t foo".
 */
struct Tool {
  /**
   * Short name of the tool.
   */
  const char *name;

  /**
   * Description (shown in "-t list").
   */
  const char *desc;

  /**
   * When to run the tool.
   */
  enum {
    /**
     * Run after parsing the command-line flags and potentially changing
     * the current working directory (as early as possible).
     */
    RUN_AFTER_FLAGS,

    /**
     * Run after loading build.ninja.
     */
    RUN_AFTER_LOAD,

    /**
     * Run after loading the build/deps logs.
     */
    RUN_AFTER_LOGS,
  } when;

  /**
   * Implementation of the tool.
   */
  NinjaMain::ToolFunc func;
};

/**
 * Print usage information.
 */
void usage(const BuildConfig &config) {
  fprintf(stderr,
"usage: shk [options] [targets...]\n"
"\n"
"if targets are unspecified, builds the 'default' target (see manual).\n"
"\n"
"options:\n"
"  --version  print Shuriken version (\"%s\")\n"
"\n"
"  -C DIR   change to DIR before doing anything else\n"
"  -f FILE  specify input build file [default=build.ninja]\n"
"\n"
"  -j N     run N jobs in parallel [default=%d, derived from CPUs available]\n"
"  -k N     keep going until N jobs fail [default=1]\n"
"  -l N     do not start new jobs if the load average is greater than N\n"
"  -n       dry run (don't run commands but act like they succeeded)\n"
"  -v       show all command lines while building\n"
"\n"
"  -t TOOL  run a subtool (use -t list to list subtools)\n"
"    terminates toplevel options; further flags are passed to the tool\n",
          kNinjaVersion, config.parallelism);
}

/**
 * Rebuild the build manifest, if necessary.
 * Returns true if the manifest was rebuilt.
 */
bool NinjaMain::rebuildManifest(const char *input_file, std::string *err) {
  std::string path = input_file;
  unsigned int slash_bits;  // Unused because this path is only used for lookup.
  if (!canonicalizePath(&path, &slash_bits, err)) {
    return false;
  }
  Node *node = _state.lookupNode(path);
  if (!node) {
    return false;
  }

  Builder builder(&_state, _config, &_deps_log, &_disk_interface);
  if (!builder.addTarget(node, err)) {
    return false;
  }

  if (builder.alreadyUpToDate()) {
    return false;  // Not an error, but we didn't rebuild.
  }

  // Even if the manifest was cleaned by a restat rule, claim that it was
  // rebuilt.  Not doing so can lead to crashes, see
  // https://github.com/ninja-build/ninja/issues/874
  return builder.build(err);
}

std::vector<Node *> NinjaMain::collectTargetsFromArgs(
    int argc, char *argv[]) throw(BuildError) {
  if (argc == 0) {
    return _state.defaultNodes();
  }

  std::vector<Node *> targets;
  for (int i = 0; i < argc; ++i) {
    targets->push_back(collectTarget(argv[i]));
  }
  return targets;
}

int toolTargetsList(const std::vector<Node *> &nodes, int depth, int indent) {
  for (const auto &n : nodes) {
    for (int i = 0; i < indent; ++i) {
      printf("  ");
    }
    const char *target = node->path().c_str();
    if (node->in_edge()) {
      printf("%s: %s\n", target, node->in_edge()->rule_->name().c_str());
      if (depth > 1 || depth <= 0) {
        toolTargetsList(node->in_edge()->inputs_, depth - 1, indent + 1);
      }
    } else {
      printf("%s\n", target);
    }
  }
  return 0;
}

int toolTargetsSourceList(State *state) {
  for (const auto &edge : state->edges_) {
    for (const auto &inp : edge->inputs_) {
      if (!inp->in_edge()) {
        printf("%s\n", inp->path().c_str());
      }
    }
  }
  return 0;
}

int toolTargetsList(State *state, const std::string &rule_name) {
  std::set<std::string> rules;

  // Gather the outputs.
  for (const auto &edge : state->edges_) {
    if (edge->rule_->name() == rule_name) {
      for (const auto &out_node : edge->outputs_) {
        rules.insert(out_node->path());
      }
    }
  }

  // Print them.
  for (auto i = rules.begin(); i != rules.end(); ++i) {
    printf("%s\n", (*i).c_str());
  }

  return 0;
}

int toolTargetsList(State *state) {
  for (const auto &edge : state->edges_) {
    for (const auto &out_node : edge->outputs_) {
      printf(
          "%s: %s\n",
          out_node->path().c_str(),
          edge->rule_->name().c_str());
    }
  }
  return 0;
}

/**
 * Find the function to execute for \a tool_name and return it via \a func.
 * Returns a Tool, or NULL if Ninja should exit.
 */
const Tool *chooseTool(const std::string &tool_name) {
  static const Tool kTools[] = {
    { "clean", "clean built files",
      Tool::RUN_AFTER_LOAD, &NinjaMain::toolClean },
    { "commands", "list all commands required to rebuild given targets",
      Tool::RUN_AFTER_LOAD, &NinjaMain::toolCommands },
    { "deps", "show dependencies stored in the deps log",
      Tool::RUN_AFTER_LOGS, &NinjaMain::toolDeps },
    { "query", "show inputs/outputs for a path",
      Tool::RUN_AFTER_LOGS, &NinjaMain::toolQuery },
    { "targets",  "list targets by their rule or depth in the DAG",
      Tool::RUN_AFTER_LOAD, &NinjaMain::toolTargets },
    { "compdb",  "dump JSON compilation database to stdout",
      Tool::RUN_AFTER_LOAD, &NinjaMain::toolCompilationDatabase },
    { "recompact",  "recompacts ninja-internal data structures",
      Tool::RUN_AFTER_LOAD, &NinjaMain::toolRecompact },
    { NULL, NULL, Tool::RUN_AFTER_FLAGS, NULL }
  };

  if (tool_name == "list") {
    printf("ninja subtools:\n");
    for (const Tool *tool = &kTools[0]; tool->name; ++tool) {
      if (tool->desc) {
        printf("%10s  %s\n", tool->name, tool->desc);
      }
    }
    return NULL;
  }

  for (const Tool *tool = &kTools[0]; tool->name; ++tool) {
    if (tool->name == tool_name) {
      return tool;
    }
  }

  std::vector<const char *> words;
  for (const Tool *tool = &kTools[0]; tool->name; ++tool) {
    words.push_back(tool->name);
  }
  const char *suggestion = spellcheckStringV(tool_name, words);
  if (suggestion) {
    fatal("unknown tool '%s', did you mean '%s'?",
          tool_name.c_str(), suggestion);
  } else {
    fatal("unknown tool '%s'", tool_name.c_str());
  }
  return NULL;  // Not reached.
}

/**
 * Open the deps log: load it, then open for writing.
 * @return false on error.
 */
bool NinjaMain::openDepsLog(bool recompact_only) {
  std::string path = ".ninja_deps";
  if (!_build_dir.empty())
    path = _build_dir + "/" + path;

  std::string err;
  if (!_deps_log.Load(path, &_state, &err)) {
    error("loading deps log %s: %s", path.c_str(), err.c_str());
    return false;
  }
  if (!err.empty()) {
    // Hack: Load() can return a warning via err by returning true.
    Warning("%s", err.c_str());
    err.clear();
  }

  if (recompact_only) {
    bool success = _deps_log.Recompact(path, &err);
    if (!success)
      error("failed recompaction: %s", err.c_str());
    return success;
  }

  if (!_config.dry_run) {
    if (!_deps_log.OpenForWrite(path, &err)) {
      error("opening deps log: %s", err.c_str());
      return false;
    }
  }

  return true;
}

bool NinjaMain::ensureBuildDirExists() {
  _build_dir = _state.bindings_.lookupVariable("builddir");
  if (!_build_dir.empty() && !_config.dry_run) {
    if (!_disk_interface.MakeDirs(_build_dir + "/.") && errno != EEXIST) {
      error("creating build directory %s: %s",
            _build_dir.c_str(), strerror(errno));
      return false;
    }
  }
  return true;
}

int NinjaMain::runBuild(int argc, char **argv) {
  std::vector<Node *> targets;
  try {
    targets = collectTargetsFromArgs(argc, argv);
  } catch (const BuildError &error) {
    error("%s", error.what());
    return 1;
  }

  _disk_interface.allowStatCache(g_experimental_statcache);

  std::string err;
  Builder builder(&_state, _config, &_deps_log, &_disk_interface);
  for (size_t i = 0; i < targets.size(); ++i) {
    if (!builder.AddTarget(targets[i], &err)) {
      if (!err.empty()) {
        error("%s", err.c_str());
        return 1;
      } else {
        // Added a target that is already up-to-date; not really
        // an error.
      }
    }
  }

  // Make sure restat rules do not see stale timestamps.
  _disk_interface.allowStatCache(false);

  if (builder.alreadyUpToDate()) {
    printf("shk: no work to do.\n");
    return 0;
  }

  if (!builder.build(&err)) {
    printf("shk: build stopped: %s.\n", err.c_str());
    if (err.find("interrupted by user") != string::npos) {
      return 2;
    }
    return 1;
  }

  return 0;
}

#ifdef _MSC_VER

/**
 * This handler processes fatal crashes that you can't catch
 * Test example: C++ exception in a stack-unwind-block
 * Real-world example: ninja launched a compiler to process a tricky
 * C++ input file. The compiler got itself into a state where it
 * generated 3 GB of output and caused ninja to crash.
 */
void terminateHandler() {
  createWin32MiniDump(NULL);
  fatal("terminate handler called");
}

/**
 * On Windows, we want to prevent error dialogs in case of exceptions.
 * This function handles the exception, and writes a minidump.
 */
int ExceptionFilter(unsigned int code, struct _EXCEPTION_POINTERS *ep) {
  error("exception: 0x%X", code);  // e.g. EXCEPTION_ACCESS_VIOLATION
  fflush(stderr); 
  createWin32MiniDump(ep);
  return EXCEPTION_EXECUTE_HANDLER;
}

#endif  // _MSC_VER

/**
 * Parse argv for command-line options.
 * Returns an exit code, or -1 if Ninja should continue.
 */
int ReadFlags(int *argc, char ***argv,
              Options *options, BuildConfig *config) {
  config->parallelism = guessParallelism();

  enum { OPT_VERSION = 1 };
  const option kLongOptions[] = {
    { "help", no_argument, NULL, 'h' },
    { "version", no_argument, NULL, OPT_VERSION },
    { NULL, 0, NULL, 0 }
  };

  int opt;
  while (!options->tool &&
         (opt = getopt_long(*argc, *argv, "f:j:k:l:nt:v:C:h", kLongOptions,
                            NULL)) != -1) {
    switch (opt) {
      case 'f':
        options->input_file = optarg;
        break;
      case 'j': {
        char *end;
        int value = strtol(optarg, &end, 10);
        if (*end != 0 || value <= 0) {
          fatal("invalid -j parameter");
        }
        config->parallelism = value;
        break;
      }
      case 'k': {
        char *end;
        int value = strtol(optarg, &end, 10);
        if (*end != 0) {
          fatal("-k parameter not numeric; did you mean -k 0?");
        }

        // We want to go until N jobs fail, which means we should allow
        // N failures and then stop.  For N <= 0, INT_MAX is close enough
        // to infinite for most sane builds.
        config->failures_allowed = value > 0 ? value : INT_MAX;
        break;
      }
      case 'l': {
        char *end;
        double value = strtod(optarg, &end);
        if (end == optarg) {
          fatal("-l parameter not numeric: did you mean -l 0.0?");
        }
        config->max_load_average = value;
        break;
      }
      case 'n':
        config->dry_run = true;
        break;
      case 't':
        options->tool = chooseTool(optarg);
        if (!options->tool) {
          return 0;
        }
        break;
      case 'v':
        config->verbosity = BuildConfig::VERBOSE;
        break;
      case 'C':
        options->working_dir = optarg;
        break;
      case OPT_VERSION:
        printf("%s\n", kNinjaVersion);
        return 0;
      case 'h':
      default:
        usage(*config);
        return 1;
    }
  }
  *argv += optind;
  *argc -= optind;

  return -1;
}

int real_main(int argc, char **argv) {
  BuildConfig config;
  Options options = {};
  options.input_file = "build.ninja";

  setvbuf(stdout, NULL, _IOLBF, BUFSIZ);

  int exit_code = ReadFlags(&argc, &argv, &options, &config);
  if (exit_code >= 0)
    return exit_code;

  if (options.working_dir) {
    // The formatting of this string, complete with funny quotes, is
    // so Emacs can properly identify that the cwd has changed for
    // subsequent commands.
    // Don't print this if a tool is being used, so that tool output
    // can be piped into a file without this string showing up.
    if (!options.tool)
      printf("shk: Entering directory `%s'\n", options.working_dir);
    if (chdir(options.working_dir) < 0) {
      fatal("chdir to '%s' - %s", options.working_dir, strerror(errno));
    }
  }

  if (options.tool && options.tool->when == Tool::RUN_AFTER_FLAGS) {
    // None of the RUN_AFTER_FLAGS actually use a NinjaMain, but it's needed
    // by other tools.
    NinjaMain ninja(config);
    return (ninja.*options.tool->func)(argc, argv);
  }

  // Limit number of rebuilds, to prevent infinite loops.
  const int kCycleLimit = 100;
  for (int cycle = 1; cycle <= kCycleLimit; ++cycle) {
    NinjaMain ninja(config);

    Manifest manifest;
    try {
      manifest = parseManifest(paths, _file_system, options.input_file);
    } catch (const IoError &error) {
      error("%s", error.what());
    } catch (const ParseError &error) {
      error("%s", error.what());
    }

    if (options.tool && options.tool->when == Tool::RUN_AFTER_LOAD) {
      return (ninja.*options.tool->func)(argc, argv);
    }

    if (!ninja.ensureBuildDirExists() ||
        !ninja.openDepsLog()) {
      return 1;
    }

    if (options.tool && options.tool->when == Tool::RUN_AFTER_LOGS) {
      return (ninja.*options.tool->func)(argc, argv);
    }

    // Attempt to rebuild the manifest before building anything else
    std::string err;
    if (ninja.rebuildManifest(options.input_file, &err)) {
      // In dry_run mode the regeneration will succeed without changing the
      // manifest forever. Better to return immediately.
      if (config.dry_run) {
        return 0;
      }
      // Start the build over with the new manifest.
      continue;
    } else if (!err.empty()) {
      error("rebuilding '%s': %s", options.input_file, err.c_str());
      return 1;
    }

    return ninja.runBuild(argc, argv);
  }

  error("manifest '%s' still dirty after %d tries\n",
      options.input_file, kCycleLimit);
  return 1;
}

}  // anonymous namespace

int main(int argc, char **argv) {
#if defined(_MSC_VER)
  // Set a handler to catch crashes not caught by the __try..__except
  // block (e.g. an exception in a stack-unwind-block).
  std::set_terminate(terminateHandler);
  __try {
    // Running inside __try ... __except suppresses any Windows error
    // dialogs for errors such as bad_alloc.
    return real_main(argc, argv);
  }
  __except(ExceptionFilter(GetExceptionCode(), GetExceptionInformation())) {
    // Common error situations return exitCode=1. 2 was chosen to
    // indicate a more serious problem.
    return 2;
  }
#else
  return real_main(argc, argv);
#endif
}

}  namespace shk
