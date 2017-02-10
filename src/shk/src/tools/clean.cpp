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

#include "clean.h"

#include <errno.h>

#include "../build.h"
#include "../cleaning_file_system.h"
#include "../dry_run_command_runner.h"
#include "../dummy_build_status.h"
#include "../log/dummy_invocation_log.h"
#include "../util.h"

namespace shk {

/**
 * Tool for cleaning files that have been generated as part of building with
 * shk. It works by doing a build with a file system that doesn't create
 * things but does remove things, and a dummy command runner.
 *
 * The reason it's done this way is that it allows us to re-use the dependency
 * tracking code of the build system, used when cleaning only certain targets.
 */
int toolClean(int argc, char *argv[], const ToolParams &params) {
  std::vector<Path> specified_outputs;
  try {
    specified_outputs = interpretPaths(
        params.paths, params.indexed_manifest.manifest, argc, argv);
  } catch (const BuildError &build_error) {
    error("%s", build_error.what());
    return 1;
  }

  DummyInvocationLog invocation_log;
  CleaningFileSystem cleaning_file_system(params.file_system);

  try {
    deleteStaleOutputs(
        params.file_system,
        invocation_log,
        params.indexed_manifest.step_hashes,
        params.invocations);
  } catch (const IoError &io_error) {
    printf("shk: failed to clean stale outputs: %s\n", io_error.what());
    return 1;
  }

  try {
    const auto result = build(
        params.clock,
        cleaning_file_system,
        *makeDryRunCommandRunner(),
        [](int total_steps) {
          return std::unique_ptr<BuildStatus>(new DummyBuildStatus);
        },
        invocation_log,
        1,
        specified_outputs,
        params.indexed_manifest,
        params.invocations);

    switch (result) {
    case BuildResult::NO_WORK_TO_DO:
    case BuildResult::SUCCESS:
      break;
    case BuildResult::INTERRUPTED:
      printf("shk: clean interrupted by user.\n");
      return 2;
    case BuildResult::FAILURE:
      // Should not happen; we're using the dry run command runner
      printf("shk: clean failed: internal error.\n");
      return 1;
    }
  } catch (const IoError &io_error) {
    printf("shk: clean failed: %s\n", io_error.what());
    return 1;
  } catch (const BuildError &build_error) {
    printf("shk: clean failed: %s\n", build_error.what());
    return 1;
  }

  if (specified_outputs.empty()) {
    // Clean up the invocation log only if we're cleaning everything
    try {
      // Use cleaning_file_system to make sure the file is counted
      cleaning_file_system.unlink(params.invocation_log_path);
    } catch (const IoError &io_error) {
      if (io_error.code == ENOENT) {
        // We don't care
      } else {
        printf("shk: failed to clean invocation log: %s\n", io_error.what());
        return 1;
      }
    }
  }

  int count = cleaning_file_system.getRemovedCount();
  printf("shk: cleaned %d file%s.\n", count, count == 1 ? "" : "s");

  return 0;
}

}  // namespace shk
