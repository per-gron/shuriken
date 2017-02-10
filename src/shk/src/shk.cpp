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
#include "cmd/dry_run_command_runner.h"
#include "cmd/limited_command_runner.h"
#include "cmd/real_command_runner.h"
#include "cmd/tracing_command_runner.h"
#include "edit_distance.h"
#include "fs/dry_run_file_system.h"
#include "fs/file_lock.h"
#include "fs/persistent_file_system.h"
#include "log/delayed_invocation_log.h"
#include "log/dry_run_invocation_log.h"
#include "log/invocations.h"
#include "log/persistent_invocation_log.h"
#include "manifest/indexed_manifest.h"
#include "status/terminal_build_status.h"
#include "tools/clean.h"
#include "tools/compilation_database.h"
#include "tools/deps.h"
#include "tools/query.h"
#include "tools/recompact.h"
#include "tools/targets.h"
#include "tools/tool_params.h"
#include "util.h"
#include "version.h"

namespace shk {
namespace {

/**
 * The type of functions that are the entry points to tools (subcommands).
 */
using ToolFunc = int (*)(int, char**, const ToolParams &);

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
     * Run after loading build.ninja.
     */
    RUN_AFTER_LOAD,

    /**
     * Run after reading (but not opening for writing) the invocation log.
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
        _real_file_system(persistentFileSystem()),
        _dry_run_file_system(dryRunFileSystem(*_real_file_system)),
        _file_system(config.dry_run ?
            *_dry_run_file_system : *_real_file_system),
        _paths(_file_system) {}

  void parseManifest(const std::string &input_file) throw(IoError, ParseError);

  std::string invocationLogPath() const;

  /**
   * Load the invocation log.
   *
   * @return false on error.
   */
  bool readInvocationLog(bool will_run_tool);

  /**
   * Open the invocation log for writing. Must be called after successfully
   * calling readInvocationLog.
   *
   * @return false on error.
   */
  bool openInvocationLog();

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
    return _file_system;
  }

  const Invocations &invocations() const {
    return _invocations;
  }

  const IndexedManifest &indexedManifest() const {
    return _indexed_manifest;
  }

  /**
   * Build the targets listed on the command line.
   * @return an exit code.
   */
  int runBuild(int argc, char **argv);

  /**
   * Lower level than the other runBuild method: Used both for the main build
   * and for rebuilding the manifest.
   */
  BuildResult runBuild(
      const std::vector<Path> &specified_outputs) throw(BuildError, IoError);

 private:
  const BuildConfig _config;
  const std::unique_ptr<FileSystem> _real_file_system;
  const std::unique_ptr<FileSystem> _dry_run_file_system;
  FileSystem &_file_system;
  Paths _paths;
  Invocations _invocations;
  InvocationLogParseResult::ParseData _invocation_parse_data;
  std::unique_ptr<FileLock> _invocation_log_lock;
  std::unique_ptr<InvocationLog> _invocation_log;
  IndexedManifest _indexed_manifest;
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
  const auto path = _paths.get(input_file);

  if (_indexed_manifest.output_file_map.count(path) == 0) {
    // No rule generates the manifest file. There is nothing to do.
    return false;
  }

  try {
    const auto result = runBuild({ path });
    switch (result) {
    case BuildResult::NO_WORK_TO_DO:
      return false;
    case BuildResult::SUCCESS:
      return true;
    case BuildResult::INTERRUPTED:
      *err = "build interrupted by user.";
      return false;
    case BuildResult::FAILURE:
      *err = "subcommand(s) failed.";
      return false;
    }
  } catch (const BuildError &build_error) {
    *err = std::string("BuildError: ") + build_error.what();
    return false;
  }
}

/**
 * Find the function to execute for \a tool_name and return it via \a func.
 * Returns a Tool, or NULL if Shuriken should exit.
 */
const Tool *chooseTool(const std::string &tool_name) {
  static const Tool kTools[] = {
    { "clean", "clean built files",
      Tool::RUN_AFTER_LOG, &toolClean },
    { "deps", "show dependencies stored in the invocation log",
      Tool::RUN_AFTER_LOG, &toolDeps },
    { "query", "show inputs/outputs for a path",
      Tool::RUN_AFTER_LOG, &toolQuery },
    { "targets",  "list targets by their rule or depth in the DAG",
      Tool::RUN_AFTER_LOAD, &toolTargets },
    { "compdb",  "dump JSON compilation database to stdout",
      Tool::RUN_AFTER_LOAD, &toolCompilationDatabase },
    { "recompact",  "recompacts shuriken-internal data structures",
      Tool::RUN_AFTER_LOG, &toolRecompact },
    { NULL, NULL, Tool::RUN_AFTER_LOAD, NULL }
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
  _indexed_manifest = IndexedManifest(
      ::shk::parseManifest(_paths, _file_system, input_file));
}

std::string ShurikenMain::invocationLogPath() const {
  std::string path = ".shk_log";
  if (!_indexed_manifest.manifest.build_dir.empty()) {
    path = _indexed_manifest.manifest.build_dir + "/" + path;
  }
  return path;
}

bool ShurikenMain::readInvocationLog(bool will_run_tool) {
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
    parse_result = parsePersistentInvocationLog(_paths, _file_system, path);
    _invocations = std::move(parse_result.invocations);
    _invocation_parse_data =  std::move(parse_result.parse_data);
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

  // Don't recompact if we're running a tool. This is partly to avoid unexpected
  // behavior, but mostly to avoid recompacting if the recompact tool is already
  // going to do it.
  if (!will_run_tool && parse_result.needs_recompaction) {
    printf("recompacting build log...\n");
    try {
      recompactPersistentInvocationLog(
          _file_system, getTime, _invocations, path);
    } catch (const IoError &err) {
      error("failed recompaction: %s", err.what());
      return false;
    }
  }

  return true;
}

bool ShurikenMain::openInvocationLog() {
  const auto path = invocationLogPath();

  if (_config.dry_run) {
    _invocation_log = std::unique_ptr<InvocationLog>(
        new DryRunInvocationLog());
  } else {
    try {
      mkdirsFor(_file_system, path);
    } catch (const IoError &io_error) {
      error(
          "creating directory for invocation log %s: %s",
          path.c_str(), strerror(errno));
      return false;
    }

    try {
      _invocation_log = delayedInvocationLog(
          getTime,
          openPersistentInvocationLog(
              _file_system,
              getTime,
              path,
              std::move(_invocation_parse_data)));
    } catch (const IoError &io_error) {
      error("opening invocation log: %s", io_error.what());
      return false;
    }
  }

  return true;
}

BuildResult ShurikenMain::runBuild(
    const std::vector<Path> &specified_outputs) throw(BuildError, IoError) {
  const auto command_runner = _config.dry_run ?
      makeDryRunCommandRunner() :
      makeLimitedCommandRunner(
        getLoadAverage,
        _config.max_load_average,
        _config.parallelism,
        makeTracingCommandRunner(
            _file_system,
            makeRealCommandRunner()));

  return build(
      getTime,
      _file_system,
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
      specified_outputs,
      _indexed_manifest,
      _invocations);
}

int ShurikenMain::runBuild(int argc, char **argv) {
  std::vector<Path> specified_outputs;
  try {
    specified_outputs = interpretPaths(_paths, _indexed_manifest.manifest, argc, argv);
  } catch (const BuildError &build_error) {
    error("%s", build_error.what());
    return 1;
  }

  try {
    deleteStaleOutputs(
        _file_system,
        *_invocation_log,
        _indexed_manifest.step_hashes,
        _invocations);
  } catch (const IoError &io_error) {
    printf("shk: failed to clean stale outputs: %s\n", io_error.what());
    return 1;
  }

  try {
    const auto result = runBuild(specified_outputs);

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

    ToolParams tool_params = {
        getTime,
        shk.paths(),
        shk.invocations(),
        shk.indexedManifest(),
        shk.fileSystem(),
        shk.invocationLogPath() };

    if (options.tool && options.tool->when == Tool::RUN_AFTER_LOAD) {
      return options.tool->func(argc, argv, tool_params);
    }

    if (!shk.readInvocationLog(!!options.tool)) {
      return 1;
    }

    if (options.tool && options.tool->when == Tool::RUN_AFTER_LOG) {
      return options.tool->func(argc, argv, tool_params);
    }

    // It is necessary to open the invocation log after running the tools,
    // because the recompact tool will unlink and recreate the invocation
    // log file.
    if (!shk.openInvocationLog()) {
      return 1;
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
