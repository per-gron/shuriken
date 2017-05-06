// Copyright 2017 Per Grön. All Rights Reserved.
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

#pragma once

#include <unordered_set>
#include <vector>

#include "build_error.h"
#include "clock.h"
#include "cmd/command_runner.h"
#include "fs/file_system.h"
#include "log/invocation_log.h"
#include "log/invocations.h"
#include "manifest/compiled_manifest.h"
#include "manifest/step.h"
#include "optional.h"
#include "status/build_status.h"

namespace shk {

/**
 * Get the Path to a given output file for a step in the manifest. This handles
 * the ^ command line interface syntax.
 */
StepIndex interpretPath(
    const CompiledManifest &manifest,
    std::string &&path) throw(BuildError);

/**
 * Takes command line arguments and calls interpretPath on each of them.
 */
std::vector<StepIndex> interpretPaths(
    const CompiledManifest &manifest,
    int argc,
    char *argv[]) throw(BuildError);

/**
 * Like detail::computeStepsToBuild, but with a more convenient interface for
 * use by tools.
 */
std::vector<StepIndex> computeStepsToBuild(
    const CompiledManifest &compiled_manifest,
    int argc,
    char *argv[0]) throw(BuildError);

namespace detail {
/**
 * "Map" of StepIndex => bool that indicates if the Step has been built before
 * and at the time the build was started, its direct inputs and outputs were
 * unchanged since the last time its command was run.
 *
 * That a step is "clean" in this sense does not imply that the step will not
 * be re-run during the build, because it might depend on a file that will
 * change during the build.
 *
 * This variable is used during the initial discardCleanSteps phase where
 * clean steps are marked as already done, and also by restat steps when their
 * outputs don't change.
 */
using CleanSteps = std::vector<bool>;

/**
 * "Map" of index in Invocations::fingerprints => MatchesResult with information
 * about if each fingerprint matches or not.
 *
 * FingerprintMatchesMemo is precalculated for all fingerprints that are
 * actually used by the build. The others will be empty optionals.
 *
 * Because FingerprintMatchesMemo is used at the start of a build, each entry
 * (which represents a file) will become invalid after it is overwritten by
 * build steps that are invoked.
 */
using FingerprintMatchesMemo = std::vector<Optional<MatchesResult>>;

/**
 * During the build, the Build object has one StepNode for each Step in the
 * Manifest. The StepNode contains information about dependencies between
 * steps in a format that is efficient when building.
 */
struct StepNode {
  /**
   * List of steps that depend on this step.
   *
   * When a build step is completed, the builder visits the StepNode for each
   * dependent step and decrements the dependencies counter. If the counter
   * reaches zero, that StepNode is ready to be built and can be added to the
   * Build::ready_steps list.
   */
  std::vector<StepIndex> dependents;

  /**
   * The number of not yet built steps that this step depends on.
   */
  int dependencies = 0;

  /**
   * Starts as true and is switched to false if any of the build step's direct
   * dependencies have been built during this build, or if something happens
   * that makes it impossible to know if direct dependencies have been built or
   * not.
   *
   * If a step was clean in the beginning of the build and none of its direct
   * dependencies have been built, then it can be skipped, without even touching
   * the file system.
   */
  bool no_direct_dependencies_built = true;

  /**
   * true if the user has asked to build this step or any step that depends on
   * this step. If false, the step should not be run even if it is dirty.
   *
   * This piece of information is used only when computing the initial list of
   * steps that are ready to be built; after that it is not needed because
   * dependents and dependencies never point to or from a step that should not
   * be built.
   */
  bool should_build = false;

  /**
   * Used when computing the Build graph in order to detect cycles.
   */
  bool currently_visited = false;
};

/**
 * Build is the data structure that keeps track of intermediary internal
 * information necessary to perform the build, for example the build steps that
 * are left to do in the build, and helps to efficiently provide information
 * about what to do next when a build step has completed.
 */
struct Build {
  /**
   * Take a step by index (which must be in the ready_steps list) and mark it
   * as done; remove it from the ready_steps list and add any new steps that
   * might have become ready to that list.
   *
   * output_file_ids is a list that has FileIds of all the files that this build
   * step wrote to.
   */
  void markStepNodeAsDone(
      StepIndex step_idx,
      const std::vector<FileId> &output_file_ids);

  /**
   * Create a Build object suitable for use as a starting point for the build.
   */
  static Build construct(
      const CompiledManifest &manifest,
      const Invocations &invocations,
      size_t failures_allowed,
      std::vector<StepIndex> &&steps_to_build) throw(BuildError);

  /**
   * Before the actual build is performed, this function goes through the build
   * graph and removes steps that don't need to be built because they are
   * already built.
   *
   * Returns the number of discarded steps (excluding phony steps).
   */
  int discardCleanSteps(
      const Invocations &invocations,
      const FingerprintMatchesMemo &fingerprint_matches_memo,
      StepsView steps,
      const CleanSteps &clean_steps);

  /**
   * step_nodes.size() == manifest.steps.size()
   *
   * step_nodes contains step dependency information in an easily accessible
   * format.
   */
  std::vector<StepNode> step_nodes;

  /**
   * List of steps that are ready to be run.
   */
  std::vector<StepIndex> ready_steps;

  /**
   * The number of commands that are allowed to fail before the build stops. A
   * value of 0 means that too many commands have failed and the build should
   * stop.
   */
  int remaining_failures = 0;

  /**
   * The number of commands that have been run as part of the build, excluding
   * phony build steps.
   */
  size_t invoked_commands = 0;

  /**
   * Files that have been written to during the build. This is used when
   * invoking subsequent build steps when computing if they are already clean
   * and don't need to be invoked (similar to Ninja's "restat" behavior).
   */
  std::unordered_map<FileId, Hash> written_files;

  /**
   * Files that have been written to, or that have been written to by build
   * steps that have been skipped because they were clean, during the build so
   * far.
   *
   * This is used to calculate ignored_deps and additional_deps prior to logging
   * each build step.
   *
   * This map does not contain the file ids of generator steps.
   */
  std::unordered_map<FileId, StepIndex> output_files;
};

/**
 * There are a bunch of functions in this file that take more or less the same
 * parameters, and quite many at that. The point of this struct is to avoid
 * having to pass all of them explicitly, which just gets overly verbose, hard
 * to read and painful to change.
 *
 * BuildCommandParameters is supposed to stay the same for the duration of a
 * build (although it may have pointers to things that change during the build).
 */
struct BuildCommandParameters {
  BuildCommandParameters(
      const Clock &clock,
      FileSystem &file_system,
      CommandRunner &command_runner,
      BuildStatus &build_status,
      InvocationLog &invocation_log,
      const Invocations &invocations,
      const CleanSteps &clean_steps,
      const CompiledManifest &manifest,
      const FingerprintMatchesMemo &fingerprint_matches_memo,
      Build &build)
      : clock(clock),
        file_system(file_system),
        command_runner(command_runner),
        build_status(build_status),
        invocations(invocations),
        invocation_log(invocation_log),
        clean_steps(clean_steps),
        manifest(manifest),
        fingerprint_matches_memo(fingerprint_matches_memo),
        build(build) {}

  const Clock &clock;
  FileSystem &file_system;
  CommandRunner &command_runner;
  BuildStatus &build_status;
  const Invocations &invocations;
  InvocationLog &invocation_log;
  const CleanSteps &clean_steps;
  const CompiledManifest &manifest;
  const FingerprintMatchesMemo &fingerprint_matches_memo;
  Build &build;
};

/**
 * Find the file ids for each of the outputs of a given build step that has been
 * run in the past and is recorded in the invocation log. For this function to
 * do what it is supposed to to, the provided build step must be clean (so that
 * the Invocations object is up to date).
 *
 * Returns an empty vector for generator steps.
 */
std::vector<FileId> outputFileIdsForBuildStep(
    const Invocations &invocations,
    const FingerprintMatchesMemo &fingerprint_matches_memo,
    Step step);

/**
 * Given a map of written file ids => StepIndex and a list of file ids, find the
 * sorted list of StepIndex-es that the list of file ids refer to. File ids that
 * are not in written_files are ignored.
 */
std::vector<StepIndex> usedDependencies(
    const std::unordered_map<FileId, StepIndex> &written_files,
    const std::vector<FileId> &input_file_ids);

/**
 * Given a step and a sorted list of step indices that a build invocation
 * actually used, compute ignored_dependencies and additional_dependencies to
 * write to the invocation log.
 *
 * This is a helper method for the other ignoredAndAdditionalDependencies.
 */
std::pair<std::vector<uint32_t>, std::vector<Hash>>
ignoredAndAdditionalDependencies(
    StepsView steps,
    Step step,
    const std::vector<StepIndex> &used_dependencies);

/**
 * Given a map of all the written files so far, a step and a list of file ids
 * that the step read from, compute ignored_dependencies and
 * additional_dependencies to write to the invocation log.
 */
std::pair<std::vector<uint32_t>, std::vector<Hash>>
ignoredAndAdditionalDependencies(
    const std::unordered_map<FileId, StepIndex> &written_files,
    StepsView steps,
    Step step,
    const std::vector<FileId> &input_file_ids);

/**
 * Returns true if the invocation log says that the step with index
 * possibly_ignored_step is ignored by the step with hash
 * possibly_ignoring_step_hash.
 */
bool stepIsIgnored(
    const Invocations &invocations,
    const Hash &possibly_ignoring_step_hash,
    StepIndex possibly_ignored_step);

/**
 * Find the steps that should be built. If there are no specified steps, this
 * function will use defaults specified in the manifest, or find the root nodes
 * to build.
 *
 * The returned array may contain duplicate values.
 *
 * Uses defaults or the root steps.
 */
std::vector<StepIndex> computeStepsToBuild(
    const CompiledManifest &manifest,
    std::vector<StepIndex> &&specified_steps) throw(BuildError);

/**
 * Does all the fingerprintMatches calls necessary to compute a
 * FingerprintMatchesMemo object for a given set of used fingerprints.
 */
FingerprintMatchesMemo computeFingerprintMatchesMemo(
    FileSystem &file_system,
    const std::vector<std::pair<nt_string_view, const Fingerprint &>> &
        fingerprints,
    const std::vector<uint32_t> used_fingerprints);

/**
 * Checks if a build step has already been performed and does not need to be
 * run again. This is not purely a read-only action: It uses fingerprints, and
 * if the fingerprint logic wants a fresher fingerprint in the invocation log
 * for the future, isClean provides that.
 */
bool isClean(
    FileSystem &file_system,
    InvocationLog &invocation_log,
    const FingerprintMatchesMemo &fingerprint_matches_memo,
    const Invocations &invocations,
    Step step) throw(IoError);

/**
 * Create a CleanSteps object. For more info, see the documentation for that
 * typedef.
 */
CleanSteps computeCleanSteps(
    const Clock &clock,
    FileSystem &file_system,
    InvocationLog &invocation_log,
    const Invocations &invocations,
    StepsView steps,
    const Build &build,
    const FingerprintMatchesMemo &fingerprint_memo) throw(IoError);

/**
 * Prior to invoking the command for a step, delete the files that it previously
 * created.
 */
void deleteOldOutputs(
    FileSystem &file_system,
    const Invocations &invocations,
    InvocationLog &invocation_log,
    const Hash &step_hash) throw(IoError);

/**
 * This function is called when Shuriken is just about to invoke a build
 * command. It does a quick check if it's possible to just not run the command
 * because it's already clean. (This is similar to restat rules in Ninja.)
 *
 * This function is never slower than stat-ing all inputs, which ought to be
 * either fast (if it's already in the OS file system cache), or be fast in
 * the long run, since if the command turns out to be clean it was worth it
 * and if it turns out that it was dirty, this warms up the file system cache
 * so that the files are faster to access by the build command itself.
 */
bool canSkipBuildCommand(
    FileSystem &file_system,
    const CleanSteps &clean_steps,
    const std::unordered_map<FileId, Hash> &written_files,
    const Invocations &invocations,
    const Step &step,
    StepIndex step_idx);

int countStepsToBuild(StepsView steps, const Build &build);

}  // namespace detail

enum class BuildResult {
  NO_WORK_TO_DO,
  SUCCESS,
  INTERRUPTED,
  FAILURE,
};

using MakeBuildStatus = std::function<
    std::unique_ptr<BuildStatus> (int total_steps)>;

/**
 * Delete files that were written by build steps that aren't present in the
 * manifest anymore.
 */
void deleteStaleOutputs(
    FileSystem &file_system,
    InvocationLog &invocation_log,
    StepsView steps,
    const Invocations &invocations) throw(IoError);

/**
 * Main entry point for performing a build.
 *
 * This function does not delete stale outputs. See deleteStaleOutputs.
 */
BuildResult build(
    const Clock &clock,
    FileSystem &file_system,
    CommandRunner &command_runner,
    const MakeBuildStatus &make_build_status,
    InvocationLog &invocation_log,
    size_t failures_allowed,
    std::vector<StepIndex> &&specified_steps,
    const CompiledManifest &compiled_manifest,
    const Invocations &invocations) throw(IoError, BuildError);

}  // namespace shk
