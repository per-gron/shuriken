#include "build.h"

#include <assert.h>
#include <errno.h>

#include "fs/fingerprint.h"

namespace shk {

Path interpretPath(
    Paths &paths,
    const IndexedManifest &manifest,
    std::string &&path) throw(BuildError) {
  const bool input = !path.empty() && path[path.size() - 1] == '^';
  if (input) {
    path.resize(path.size() - 1);
  }

  const auto p = paths.get(path);

  const auto search = [&](const std::vector<Path> &paths_to_search) {
    for (const auto &path_to_search : paths_to_search) {
      if (p.isSame(path_to_search)) {
        return true;
      }
    }
    return false;
  };

  for (const auto &step : manifest.steps) {
    const auto found = input ?
        search(step.dependencies) :
        search(step.outputs);
    if (found) {
      if (step.outputs.empty()) {
        throw BuildError("Step with input '" + path + "' has no output");
      } else {
        return step.outputs[0];
      }
    }
  }

  // Not found
  std::string error = "Unknown target '" + path + "'";
  if (path == "clean") {
    error += ", did you mean 'shk -t clean'?";
  } else if (path == "help") {
    error += ", did you mean 'shk -h'?";
  }
  throw BuildError(error);
}

std::vector<Path> interpretPaths(
    Paths &paths,
    const IndexedManifest &manifest,
    int argc,
    char *argv[]) throw(BuildError) {
  std::vector<Path> targets;
  for (int i = 0; i < argc; ++i) {
    targets.push_back(interpretPath(paths, manifest, argv[i]));
  }
  return targets;
}

std::vector<StepIndex> computeStepsToBuild(
    Paths &paths,
    const IndexedManifest &manifest,
    int argc,
    char *argv[0]) throw(BuildError) {
  const auto specified_outputs = interpretPaths(
      paths, manifest, argc, argv);
  return detail::computeStepsToBuild(
      manifest, specified_outputs);
}

namespace detail {

void markStepNodeAsDone(Build &build, StepIndex step_idx) {
  const auto &dependents = build.step_nodes[step_idx].dependents;
  for (const auto dependent_idx : dependents) {
    auto &dependent = build.step_nodes[dependent_idx];
    assert(dependent.dependencies);
    dependent.dependencies--;
    if (dependent.dependencies == 0) {
      build.ready_steps.push_back(dependent_idx);
    }
  }
}

std::vector<StepIndex> rootSteps(
    const std::vector<Step> &steps,
    const OutputFileMap &output_file_map) throw(BuildError) {
  std::vector<StepIndex> result;
  // Assume that all steps are roots until we find some step that has an input
  // that is in a given step's list of outputs. Such steps are not roots.
  std::vector<bool> roots(steps.size(), true);

  for (size_t i = 0; i < steps.size(); i++) {
    for (const auto &input : steps[i].dependencies) {
      const auto it = output_file_map.find(input);
      if (it != output_file_map.end()) {
        roots[it->second] = false;
      }
    }
  }

  for (size_t i = 0; i < steps.size(); i++) {
    if (roots[i]) {
      result.push_back(i);
    }
  }

  if (result.empty() && !steps.empty()) {
    throw BuildError(
        "Could not determine root nodes of build graph. Cyclic dependency?");
  }

  return result;
}

/**
 * Compute indices of steps to build from a list of output paths. Helper for
 * computeStepsToBuild, used both for defaults specified in the manifest and
 * paths specified from the command line.
 */
std::vector<StepIndex> computeStepsToBuildFromPaths(
    const std::vector<Path> &paths,
    const OutputFileMap &output_file_map) throw(BuildError) {
  std::vector<StepIndex> result;
  for (const auto &default_path : paths) {
    const auto it = output_file_map.find(default_path);
    if (it == output_file_map.end()) {
      throw BuildError(
          "Specified target does not exist: " + default_path.original());
    }
    // This may result in duplicate values in result, which is ok
    result.push_back(it->second);
  }
  return result;
}

std::vector<StepIndex> computeStepsToBuild(
    const IndexedManifest &manifest,
    const std::vector<Path> &specified_outputs) throw(BuildError) {
  if (!specified_outputs.empty()) {
    return computeStepsToBuildFromPaths(
        specified_outputs, manifest.output_file_map);
  } else if (!manifest.defaults.empty()) {
    return computeStepsToBuildFromPaths(
        manifest.defaults, manifest.output_file_map);
  } else {
    return rootSteps(manifest.steps, manifest.output_file_map);
  }
}

/**
 * Helper for computeBuild.
 *
 * Takes a list of ready-computed StepNodes and finds the inital list of steps
 * that can be built.
 */
std::vector<StepIndex> computeReadySteps(
    const std::vector<StepNode> &step_nodes) {
  std::vector<StepIndex> result;
  for (size_t i = 0; i < step_nodes.size(); i++) {
    const auto &step_node = step_nodes[i];
    if (step_node.should_build && step_node.dependencies == 0) {
      result.push_back(i);
    }
  }
  return result;
}

std::string cycleErrorMessage(const std::vector<Path> &cycle) {
  assert(!cycle.empty());

  std::string error;
  for (const auto &path : cycle) {
    error += path.original() + " -> ";
  }
  error += cycle.front().original();
  return error;
}

/**
 * In the process of calculating a build graph out of the build steps that are
 * declared in the manifest (the computeBuild function does this), Shuriken
 * traverses the build steps via its dependencies. This function helps this
 * process by taking a step and (via callback invocations) providing the files
 * that the given step depends on.
 *
 * This function operates differently on the initial build compared to
 * subsequent builds, and this difference is rather central to the whole design
 * of Shuriken and how Shuriken is different compared to Ninja. During the first
 * build, Shuriken does not care about the difference between inputs, implicit
 * dependencies and order-only dependencies; they are all dependencies and are
 * treated equally.
 *
 * On subsequent builds, Ninja treats order-only dependencies differently from
 * other dependencies, and also brings depfile dependencies into the mix by
 * counting them as part of the implicit dependencies.
 *
 * Shuriken does not do this. It doesn't have to, because it has accurate
 * dependency information from when the build step was last invoked. When there
 * is an up-to-date invocation log entry for the given step, Shuriken completely
 * ignores the dependencies declared in the manifest and uses only the
 * calculated dependencies. This simplifies the logic a bit and unties manifest
 * specified dependencies from dependencies retrieved from running the command.
 */
template<typename Callback>
void visitStepInputs(
    const Invocations &invocations,
    const IndexedManifest &manifest,
    StepIndex idx,
    Callback &&callback) {
  const auto invocation_it = invocations.entries.find(
      manifest.steps[idx].hash);
  if (invocation_it != invocations.entries.end()) {
    // There is an entry for this step in the invocation log. Use the real
    // inputs from the last invocation rather than the ones specified in the
    // manifest.
    const auto &input_files = invocation_it->second.input_files;
    for (const auto input_file_idx : input_files) {
      const auto &input_file = invocations.fingerprints[input_file_idx];
      callback(input_file.first);
    }
  } else {
    // There is no entry for this step in the invocation log.
    for (const auto &input : manifest.steps[idx].dependencies) {
      callback(input);
    }
  }
}

/**
 * Recursive helper for computeBuild. Implements the DFS traversal.
 */
void visitStep(
    const IndexedManifest &manifest,
    const Invocations &invocations,
    Build &build,
    std::vector<Path> &cycle,
    StepIndex idx) throw(BuildError) {
  auto &step_node = build.step_nodes[idx];
  if (step_node.currently_visited) {
    throw BuildError("Dependency cycle: " + cycleErrorMessage(cycle));
  }

  if (step_node.should_build) {
    // The step has already been processed.
    return;
  }
  step_node.should_build = true;

  step_node.currently_visited = true;
  visitStepInputs(
      invocations,
      manifest,
      idx,
      [&](const Path &input) {
        const auto it = manifest.output_file_map.find(input);
        if (it == manifest.output_file_map.end()) {
          // This input is not an output of some other build step.
          return;
        }

        const auto dependency_idx = it->second;
        auto &dependency_node = build.step_nodes[dependency_idx];
        dependency_node.dependents.push_back(idx);
        step_node.dependencies++;

        cycle.push_back(input);
        visitStep(
            manifest,
            invocations,
            build,
            cycle,
            dependency_idx);
        cycle.pop_back();
      });
  step_node.currently_visited = false;
}

Build computeBuild(
    const Invocations &invocations,
    const IndexedManifest &manifest,
    size_t failures_allowed,
    std::vector<StepIndex> &&steps_to_build) throw(BuildError) {
  Build build;
  build.step_nodes.resize(manifest.steps.size());

  std::vector<Path> cycle;
  cycle.reserve(32);  // Guess at largest typical build dependency depth
  for (const auto step_idx : steps_to_build) {
    visitStep(
        manifest,
        invocations,
        build,
        cycle,
        step_idx);
  }

  build.ready_steps = computeReadySteps(build.step_nodes);
  build.remaining_failures = failures_allowed;
  return build;
}

namespace {

MatchesResult checkFingerprintMatches(
    FileSystem &file_system,
    const std::vector<std::pair<Path, Fingerprint>> &fingerprints,
    size_t fingerprint_idx,
    FingerprintMatchesMemo &fingerprint_matches_memo) {
  assert(fingerprint_idx < fingerprint_matches_memo.size());
  if (!fingerprint_matches_memo[fingerprint_idx]) {
    const auto &file = fingerprints[fingerprint_idx];
    fingerprint_matches_memo[fingerprint_idx] =
        fingerprintMatches(
            file_system,
            file.first.original(),
            file.second);
  }
  return *fingerprint_matches_memo[fingerprint_idx];
}

}  // namespace shk

bool isClean(
    const Clock &clock,
    FileSystem &file_system,
    InvocationLog &invocation_log,
    FingerprintMatchesMemo &fingerprint_matches_memo,
    const Invocations &invocations,
    const Hash &step_hash) throw(IoError) {
  const auto it = invocations.entries.find(step_hash);
  if (it == invocations.entries.end()) {
    return false;
  }

  bool should_update = false;
  bool clean = true;
  const auto process_files = [&](const std::vector<size_t> &fingerprints) {
    for (const auto fingerprint_idx : fingerprints) {
      const auto match = checkFingerprintMatches(
          file_system,
          invocations.fingerprints,
          fingerprint_idx,
          fingerprint_matches_memo);
      if (!match.clean) {
        clean = false;
      }
      if (match.should_update) {
        should_update = true;
      }
    }
  };
  const auto &entry = it->second;
  process_files(entry.output_files);
  process_files(entry.input_files);

  if (should_update && clean) {
    // There is no need to update the invocation log when dirty; it will be
    // updated anyway as part of the build.
    invocation_log.relogCommand(
        step_hash,
        invocations.fingerprints,
        entry.output_files,
        entry.input_files);
  }

  return clean;
}

CleanSteps computeCleanSteps(
    const Clock &clock,
    FileSystem &file_system,
    InvocationLog &invocation_log,
    const Invocations &invocations,
    const std::vector<Step> &steps,
    const Build &build) throw(IoError) {
  assert(manifest.steps.size() == build.step_nodes.size());

  CleanSteps result(build.step_nodes.size(), false);

  FingerprintMatchesMemo fingerprint_memo;
  fingerprint_memo.resize(invocations.fingerprints.size());

  for (size_t i = 0; i < build.step_nodes.size(); i++) {
    const auto &step_node = build.step_nodes[i];
    if (!step_node.should_build) {
      continue;
    }
    const auto &step_hash = steps[i].hash;
    result[i] = isClean(
        clock,
        file_system,
        invocation_log,
        fingerprint_memo,
        invocations,
        step_hash);
  }

  return result;
}

int discardCleanSteps(
    const std::vector<Step> &steps,
    const CleanSteps &clean_steps,
    Build &build) {
  int discarded_steps = 0;

  // This function goes through and consumes build.ready_steps. While doing that
  // it adds an element to new_ready_steps for each dirty step that it
  // encounters. When this function's search is over, it replaces
  // build.ready_steps with this list.
  std::vector<StepIndex> new_ready_steps;

  // Memo map of step index => visited. This is to make sure that each step
  // is processed at most once.
  std::vector<bool> visited(build.step_nodes.size(), false);

  // This is a BFS search loop. build.ready_steps is the work stack.
  while (!build.ready_steps.empty()) {
    const auto step_idx = build.ready_steps.back();
    build.ready_steps.pop_back();

    if (visited[step_idx]) {
      continue;
    }
    visited[step_idx] = true;

    if (clean_steps[step_idx] || steps[step_idx].phony()) {
      discarded_steps++;
      markStepNodeAsDone(build, step_idx);
    } else {
      new_ready_steps.push_back(step_idx);
    }
  }

  build.ready_steps.swap(new_ready_steps);

  return discarded_steps;
}

void deleteBuildProduct(
    FileSystem &file_system,
    const Invocations &invocations,
    InvocationLog &invocation_log,
    const std::string &path) throw(IoError) {
  try {
    file_system.unlink(path);
  } catch (const IoError &error) {
    if (error.code != ENOENT) {
      throw IoError(
          std::string("Failed to unlink build product ") +
          path + ": " + error.what(),
          error.code);
    }
  }

  // Delete all ancestor directories that have been previously created by
  // builds and that have now become empty.
  auto dir = path;  // Initially point to the created file
  for (;;) {
    auto parent = dirname(dir);
    if (parent == dir) {
      // Reached root or cwd (the build directory).
      break;
    }
    dir = std::move(parent);

    const auto stat = file_system.stat(dir);
    if (stat.result != 0) {
      // Can't access the directory, can't go further.
      break;
    }
    if (invocations.created_directories.count(FileId(stat)) == 0) {
      // The directory wasn't created by a prior build step.
      break;
    }
    try {
      file_system.rmdir(dir);
      invocation_log.removedDirectory(dir);
    } catch (const IoError &error) {
      if (error.code == ENOTEMPTY) {
        // The directory is not empty. Do not remove.
        break;
      } else {
        throw error;
      }
    }
  }
}

void mkdirsForPath(
    FileSystem &file_system,
    InvocationLog &invocation_log,
    const std::string &path) throw(IoError) {
  const auto created_dirs = mkdirsFor(file_system, path);
  for (const auto &path : created_dirs) {
    invocation_log.createdDirectory(path);
  }
}

bool outputsWereChanged(
    FileSystem &file_system,
    const Invocations &invocations,
    const Hash &step_hash) throw(IoError) {
  const auto it = invocations.entries.find(step_hash);
  if (it == invocations.entries.end()) {
    return true;
  }

  for (const auto file_idx : it->second.output_files) {
    const auto &file = invocations.fingerprints[file_idx];
    const auto match = fingerprintMatches(
        file_system,
        file.first.original(),
        file.second);
    if (!match.clean) {
      return true;
    }
  }

  return false;
}

void enqueueBuildCommands(BuildCommandParameters &params) throw(IoError);

void commandDone(
    BuildCommandParameters &params,
    StepIndex step_idx,
    CommandRunner::Result &&result) throw(IoError) {
  const auto &step = params.manifest.steps[step_idx];

  // TODO(peck): Validate that the command did not read a file that is an output
  //   of a target that it does not depend on directly or indirectly.

  if (!step.depfile.empty()) {
    deleteBuildProduct(
        params.file_system,
        params.invocations,
        params.invocation_log,
        step.depfile);
  }
  if (!step.rspfile.empty() && result.exit_status != ExitStatus::FAILURE) {
    deleteBuildProduct(
        params.file_system,
        params.invocations,
        params.invocation_log,
        step.rspfile);
  }

  if (!step.phony()) {
    params.build_status.stepFinished(
        step,
        result.exit_status == ExitStatus::SUCCESS,
        result.output);
  }

  switch (result.exit_status) {
  case ExitStatus::SUCCESS:
    if (!isConsolePool(step.pool_name) && !step.phony()) {
      // The console pool gives the command access to stdin which is clearly not
      // a deterministic source. Because of this, steps using the console pool
      // are never counted as clean.
      //
      // Phony steps should also not be logged. There is nothing to log then.
      // More importantly though is that logging an empty entry for it will
      // cause the next build to believe that this step has no inputs so it will
      // immediately report the step as clean regardless of what it depends on.

      params.invocation_log.ranCommand(
          params.manifest.steps[step_idx].hash,
          std::move(result.output_files),
          std::move(result.input_files));
    }

    // TODO(peck): Implement restat rules
    if (false &&  // Ignore rather than trigger an assert.
        /* step.restat && */
        !outputsWereChanged(
            params.file_system,
            params.invocations,
            params.manifest.steps[step_idx].hash)) {
      // TODO(peck): Mark this step as clean
      assert(!"Not implemented");
    } else {
      markStepNodeAsDone(params.build, step_idx);
    }
    break;

  case ExitStatus::INTERRUPTED:
  case ExitStatus::FAILURE:
    if (params.build.remaining_failures) {
      params.build.remaining_failures--;
    }
    break;
  }

  // Feed the command runner with more commands now that this one is finished.
  enqueueBuildCommands(params);
}

void deleteOldOutputs(
    FileSystem &file_system,
    const Invocations &invocations,
    InvocationLog &invocation_log,
    const Hash &step_hash) throw(IoError) {
  const auto it = invocations.entries.find(step_hash);
  if (it == invocations.entries.end()) {
    return;
  }

  const auto &entry = it->second;
  for (const auto output_idx : entry.output_files) {
    const auto &output = invocations.fingerprints[output_idx];
    deleteBuildProduct(
        file_system,
        invocations,
        invocation_log,
        output.first.original());
  }
}

bool enqueueBuildCommand(BuildCommandParameters &params) throw(IoError) {
  if (params.build.ready_steps.empty() ||
      !params.command_runner.canRunMore() ||
      params.build.remaining_failures == 0) {
    return false;
  }

  const auto step_idx = params.build.ready_steps.back();
  const auto &step = params.manifest.steps[step_idx];
  params.build.ready_steps.pop_back();

  const auto &step_hash = params.manifest.steps[step_idx].hash;
  deleteOldOutputs(
      params.file_system,
      params.invocations,
      params.invocation_log,
      step_hash);

  if (!step.rspfile.empty()) {
    mkdirsForPath(params.file_system, params.invocation_log, step.rspfile);
    params.file_system.writeFile(step.rspfile, step.rspfile_content);
  }

  for (const auto &output : step.outputs) {
    mkdirsForPath(params.file_system, params.invocation_log, output.original());
  }

  // TODO(peck): What about pools?

  if (!step.phony()) {
    params.build_status.stepStarted(step);
    params.invoked_commands++;
  }
  params.command_runner.invoke(
      step.command,
      step.pool_name,
      [&params, step_idx](CommandRunner::Result &&result) {
        commandDone(params, step_idx, std::move(result));
      });

  return true;
}


void enqueueBuildCommands(BuildCommandParameters &params) throw(IoError) {
  while (enqueueBuildCommand(params)) {}
}

int countStepsToBuild(const std::vector<Step> &steps, const Build &build) {
  int step_count = 0;

  assert(steps.size() == build.step_nodes.size());
  for (size_t i = 0; i < steps.size(); i++) {
    const auto &step_node = build.step_nodes[i];
    const auto &step = steps[i];
    if (step_node.should_build && !step.phony()) {
      step_count++;
    }
  }

  return step_count;
}

}  // namespace detail

void deleteStaleOutputs(
    FileSystem &file_system,
    InvocationLog &invocation_log,
    const std::vector<Step> &steps,
    const Invocations &invocations) throw(IoError) {
  std::unordered_set<Hash> step_hashes_set;
  step_hashes_set.reserve(steps.size());
  for (const auto &step : steps) {
    step_hashes_set.insert(step.hash);
  }

  for (const auto &entry : invocations.entries) {
    if (step_hashes_set.count(entry.first) == 0) {
      for (const auto output_file_idx : entry.second.output_files) {
        const auto &output_file = invocations.fingerprints[output_file_idx];
        detail::deleteBuildProduct(
            file_system,
            invocations,
            invocation_log,
            output_file.first.original());
      }
      invocation_log.cleanedCommand(entry.first);
    }
  }
}

BuildResult build(
    const Clock &clock,
    FileSystem &file_system,
    CommandRunner &command_runner,
    const MakeBuildStatus &make_build_status,
    InvocationLog &invocation_log,
    size_t failures_allowed,
    const std::vector<Path> &specified_outputs,
    const IndexedManifest &manifest,
    const Invocations &invocations) throw(IoError, BuildError) {

  auto steps_to_build = detail::computeStepsToBuild(
      manifest, specified_outputs);

  auto build = detail::computeBuild(
      invocations,
      manifest,
      failures_allowed,
      std::move(steps_to_build));

  const auto clean_steps = detail::computeCleanSteps(
      clock,
      file_system,
      invocation_log,
      invocations,
      manifest.steps,
      build);

  const auto discarded_steps = detail::discardCleanSteps(
      manifest.steps, clean_steps, build);

  const auto build_status = make_build_status(
      countStepsToBuild(manifest.steps, build) - discarded_steps);

  detail::BuildCommandParameters params(
      clock,
      file_system,
      command_runner,
      *build_status,
      invocation_log,
      invocations,
      manifest,
      build);
  detail::enqueueBuildCommands(params);

  while (!command_runner.empty()) {
    if (command_runner.runCommands()) {
      return BuildResult::INTERRUPTED;
    }
  }

  if (build.remaining_failures == failures_allowed) {
    return params.invoked_commands == 0 ?
        BuildResult::NO_WORK_TO_DO :
        BuildResult::SUCCESS;
  } else {
    return BuildResult::FAILURE;
  }
}

}
