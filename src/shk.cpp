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
#include <sys/time.h>

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
#include "dry_run_command_runner.h"
#include "edit_distance.h"
#include "file_lock.h"
#include "in_memory_invocation_log.h"
#include "invocations.h"
#include "limited_command_runner.h"
#include "manifest.h"
#include "persistent_file_system.h"
#include "persistent_invocation_log.h"
#include "real_command_runner.h"
#include "terminal_build_status.h"
#include "tools/clean.h"
#include "tools/commands.h"
#include "tools/compilation_database.h"
#include "tools/deps.h"
#include "tools/query.h"
#include "tools/recompact.h"
#include "tools/targets.h"
#include "tracing_command_runner.h"
#include "util.h"
#include "version.h"

namespace shk {
namespace {

/**
 * The type of functions that are the entry points to tools (subcommands).
 */
using ToolFunc = int (*)(int, char**);

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
     * Run after loading the invocation log.
     */
    RUN_AFTER_LOG,
  } when;

  /**
   * Implementation of the tool.
   */
  ToolFunc func;
};

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

time_t getTime() {
  timeval t;
  if (gettimeofday(&t, NULL)) {
    throw BuildError("failed to get current time");
  }
  return t.tv_sec;
}

/**
 * The Shuriken main() loads up a series of data structures; various tools need
 * to poke into these, so store them as fields on an object.
 */
struct ShurikenMain {
  ShurikenMain(
      const BuildConfig &config)
      : _config(config),
        _file_system(persistentFileSystem()),
        _paths(*_file_system) {}

  std::vector<Path> interpretPaths(
      int argc, char *argv[]) throw(BuildError);

  void parseManifest(const std::string &input_file) throw(IoError, ParseError);

  std::string invocationLogPath() const;

  /**
   * Load the invocation log and open it for writing.
   *
   * @return false on error.
   */
  bool readAndOpenInvocationLog();

  /**
   * Rebuild the manifest, if necessary.
   * Fills in \a err on error.
   * @return true if the manifest was rebuilt.
   */
  bool rebuildManifest(const char *input_file, std::string *err);

  Paths &paths() {
    return _paths;
  }

  FileSystem &fileSystem() {
    return *_file_system;
  }

  /**
   * Build the targets listed on the command line.
   * @return an exit code.
   */
  int runBuild(int argc, char **argv);

 private:
  const BuildConfig _config;
  const std::unique_ptr<FileSystem> _file_system;
  Paths _paths;
  Invocations _invocations;
  std::unique_ptr<FileLock> _invocation_log_lock;
  std::unique_ptr<InvocationLog> _invocation_log;
  Manifest _manifest;
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
bool ShurikenMain::rebuildManifest(const char *input_file, std::string *err) {
#if 0  // TODO(peck): Implement me
  const auto path = _paths.get(input_file);

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
#endif
  return false;
}

std::vector<Path> ShurikenMain::interpretPaths(
    int argc, char *argv[]) throw(BuildError) {
  std::vector<Path> targets;
  for (int i = 0; i < argc; ++i) {
    targets.push_back(interpretPath(_paths, _manifest, argv[i]));
  }
  return targets;
}

/**
 * Find the function to execute for \a tool_name and return it via \a func.
 * Returns a Tool, or NULL if Shuriken should exit.
 */
const Tool *chooseTool(const std::string &tool_name) {
  static const Tool kTools[] = {
    { "clean", "clean built files",
      Tool::RUN_AFTER_LOAD, &toolClean },
    { "commands", "list all commands required to rebuild given targets",
      Tool::RUN_AFTER_LOAD, &toolCommands },
    { "deps", "show dependencies stored in the invocation log",
      Tool::RUN_AFTER_LOG, &toolDeps },
    { "query", "show inputs/outputs for a path",
      Tool::RUN_AFTER_LOG, &toolQuery },
    { "targets",  "list targets by their rule or depth in the DAG",
      Tool::RUN_AFTER_LOAD, &toolTargets },
    { "compdb",  "dump JSON compilation database to stdout",
      Tool::RUN_AFTER_LOAD, &toolCompilationDatabase },
    { "recompact",  "recompacts shuriken-internal data structures",
      Tool::RUN_AFTER_LOAD, &toolRecompact },
    { NULL, NULL, Tool::RUN_AFTER_FLAGS, NULL }
  };

  if (tool_name == "list") {
    printf("shk subtools:\n");
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

void ShurikenMain::parseManifest(
    const std::string &input_file) throw(IoError, ParseError) {
  _manifest = ::shk::parseManifest(_paths, *_file_system, input_file);
}

std::string ShurikenMain::invocationLogPath() const {
  std::string path = ".shk_log";
  if (!_manifest.build_dir.empty()) {
    path = _manifest.build_dir + "/" + path;
  }
  return path;
}

bool ShurikenMain::readAndOpenInvocationLog() {
  const auto path = invocationLogPath();
  const auto lock_path = path + ".lock";

  try {
    _invocation_log_lock.reset(new FileLock(lock_path));
  } catch (const IoError &io_error) {
    error(
        "acquiring invocation log lock %s: %s",
        lock_path.c_str(),
        io_error.what());
    return false;
  }

  InvocationLogParseResult parse_result;

  try {
    parse_result = parsePersistentInvocationLog(_paths, *_file_system, path);
    _invocations = std::move(parse_result.invocations);
    if (!parse_result.warning.empty()) {
      warning("%s", parse_result.warning.c_str());
    }
  } catch (const IoError &io_error) {
    error("loading invocation log %s: %s", path.c_str(), io_error.what());
    return false;
  } catch (const ParseError &parse_error) {
    error("parsing invocation log %s: %s", path.c_str(), parse_error.what());
    return false;
  }

  if (_config.dry_run) {
    _invocation_log = std::unique_ptr<InvocationLog>(
        new InMemoryInvocationLog());
  } else {
    try {
      mkdirsFor(*_file_system, path);
    } catch (const IoError &io_error) {
      error(
          "creating directory for invocation log %s: %s",
          path.c_str(), strerror(errno));
      return false;
    }

    try {
      _invocation_log = openPersistentInvocationLog(
          *_file_system,
          path,
          std::move(parse_result.parse_data));
    } catch (const IoError &io_error) {
      error("opening invocation log: %s", io_error.what());
      return false;
    }
  }

  return true;
}

int ShurikenMain::runBuild(int argc, char **argv) {
  std::vector<Path> targets;
  try {
    targets = interpretPaths(argc, argv);
  } catch (const BuildError &build_error) {
    error("%s", build_error.what());
    return 1;
  }

  const auto command_runner = _config.dry_run ?
      makeDryRunCommandRunner() :
      makeLimitedCommandRunner(
        getLoadAverage,
        _config.max_load_average,
        _config.parallelism,
        makeTracingCommandRunner(
            *_file_system,
            makeRealCommandRunner()));

  try {
    const auto result = build(
        getTime,
        *_file_system,
        *command_runner,
        [this](int total_steps) {
          const char * status_format = getenv("NINJA_STATUS");
          if (!status_format) {
            status_format = "[%s/%t] ";
          }

          return makeTerminalBuildStatus(
              _config.verbose,
              _config.parallelism,
              total_steps,
              status_format);
        },
        *_invocation_log,
        _config.failures_allowed,
        _manifest,
        _invocations);

    switch (result) {
    case BuildResult::NO_WORK_TO_DO:
      printf("shk: no work to do.\n");
      return 0;
    case BuildResult::SUCCESS:
      return 0;
    case BuildResult::INTERRUPTED:
      printf("shk: build interrupted by user.\n");
      return 2;
      break;
    case BuildResult::FAILURE:
      printf("shk: build failed: subcommand(s) failed.\n");
      return 1;
    }
  } catch (const IoError &io_error) {
    printf("shk: build failed: %s\n", io_error.what());
    return 1;
  } catch (const BuildError &build_error) {
    printf("shk: build failed: %s\n", build_error.what());
    return 1;
  }
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
int exceptionFilter(unsigned int code, struct _EXCEPTION_POINTERS *ep) {
  error("exception: 0x%X", code);  // e.g. EXCEPTION_ACCESS_VIOLATION
  fflush(stderr); 
  createWin32MiniDump(ep);
  return EXCEPTION_EXECUTE_HANDLER;
}

#endif  // _MSC_VER

/**
 * Parse argv for command-line options.
 * Returns an exit code, or -1 if Shuriken should continue.
 */
int readFlags(int *argc, char ***argv,
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
         (opt = getopt_long(*argc, *argv, "f:j:k:l:nt:vC:h", kLongOptions,
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
        config->verbose = true;
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

  int exit_code = readFlags(&argc, &argv, &options, &config);
  if (exit_code >= 0) {
    return exit_code;
  }

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
    // None of the RUN_AFTER_FLAGS actually use a ShurikenMain, but it's needed
    // by other tools.
    ShurikenMain shk(config);
    return options.tool->func(argc, argv);
  }

  // Limit number of rebuilds, to prevent infinite loops.
  const int kCycleLimit = 100;
  for (int cycle = 1; cycle <= kCycleLimit; ++cycle) {
    ShurikenMain shk(config);

    try {
      shk.parseManifest(options.input_file);
    } catch (const IoError &io_error) {
      error("%s", io_error.what());
      return 1;
    } catch (const ParseError &parse_error) {
      error("%s", parse_error.what());
      return 1;
    }

    if (options.tool && options.tool->when == Tool::RUN_AFTER_LOAD) {
      return options.tool->func(argc, argv);
    }

    if (!shk.readAndOpenInvocationLog()) {
      return 1;
    }

    if (options.tool && options.tool->when == Tool::RUN_AFTER_LOG) {
      return options.tool->func(argc, argv);
    }

    // Attempt to rebuild the manifest before building anything else
    std::string err;
    if (shk.rebuildManifest(options.input_file, &err)) {
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

    return shk.runBuild(argc, argv);
  }

  error("manifest '%s' still dirty after %d tries\n",
      options.input_file, kCycleLimit);
  return 1;
}

}  // anonymous namespace
}  // namespace shk

int main(int argc, char **argv) {
#if defined(_MSC_VER)
  // Set a handler to catch crashes not caught by the __try..__except
  // block (e.g. an exception in a stack-unwind-block).
  std::set_terminate(terminateHandler);
  __try {
    // Running inside __try ... __except suppresses any Windows error
    // dialogs for errors such as bad_alloc.
    return shk::real_main(argc, argv);
  }
  __except(exceptionFilter(GetExceptionCode(), GetExceptionInformation())) {
    // Common error situations return exitCode=1. 2 was chosen to
    // indicate a more serious problem.
    return 2;
  }
#else
  return shk::real_main(argc, argv);
#endif
}
