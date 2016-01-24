#include "build.h"

#include "fingerprint.h"

namespace shk {

using StepIndex = size_t;

/**
 * Map of path => index of the step that has this file as an output.
 *
 * This is useful for traversing the build graph in the direction of a build
 * step to a build step that it depends on.
 */
using OutputFileMap = std::unordered_map<Path, StepIndex>;

/**
 * "Map" of StepIndex => Hash of that step. The hash includes everything about
 * that step but not information about its dependencies.
 */
using StepHashes = std::vector<Hash>;

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
 * Build is the data structure that keeps track of the build steps that are
 * left to do in the build and helps to efficiently provide information about
 * what to do next when a build step has completed.
 */
struct Build {
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
   * interrupted is set to true when the user interrupts the build. When this
   * has happened, no more build commands should be invoked.
   */
  bool interrupted = false;

  void markStepNodeAsDone(StepIndex step_idx) {
    const auto &dependents = step_nodes[step_idx].dependents;
    for (const auto dependent_idx : dependents) {
      auto &dependent = step_nodes[dependent_idx];
      assert(dependent.dependencies);
      dependent.dependencies--;
      if (dependent.dependencies == 0) {
        ready_steps.push_back(dependent_idx);
      }
    }
  }
};

/**
 * Compute the "root steps," that is the steps that don't have an output that
 * is an input to some other step. This is the set of steps that are built if
 * there are no default statements in the manifest and no steps where
 * specifically requested to be built.
 */
std::vector<StepIndex> rootSteps(
    const std::vector<Step> &steps,
    const OutputFileMap &output_file_map) {
  std::vector<StepIndex> result;
  // Assume that all steps are roots until we find some step that has an input
  // that is in a given step's list of outputs. Such steps are not roots.
  std::vector<bool> roots(steps.size(), true);

  const auto process_inputs = [&](const std::vector<Path> &inputs) {
    for (const auto &input : inputs) {
      const auto it = output_file_map.find(input);
      if (it != output_file_map.end()) {
        roots[it->second] = false;
      }
    }
  };

  for (size_t i = 0; i < steps.size(); i++) {
    const auto &step = steps[i];
    process_input(step.inputs);
    process_input(step.implicit_inputs);
    process_input(step.dependencies);
  }

  for (size_t i = 0; i < steps.size(); i++) {
    if (roots[i]) {
      result.push_back(i);
    }
  }
  return result;
}

/**
 * Find the steps that should be built if no steps are specifically requested.
 *
 * Uses defaults or the root steps.
 */
std::vector<StepIndex> computeStepsToBuild(
    const Manifest &manifest,
    const OutputFileMap &output_file_map) throw(BuildError) {
  if (manifest.defaults.empty()) {
    return rootSteps(manifest.steps);
  } else {
    std::vector<StepIndex> result;
    for (const auto &default_path : manifest.defaults) {
      const auto it = output_file_map->find(default_path);
      if (it == output_file_map.end()) {
        throw BuildError(
            "default target does not exist: " + default_path.original());
      }
      result.push_back(it->second);
    }
    return result;
  }
}

/**
 * Throws BuildError if there exists an output file that more than one step
 * generates.
 */
OutputFileMap computeOutputFileMap(
    const std::vector<Step> &steps) throw(BuildError) {
  OutputFileMap result;

  for (size_t i = 0; i < steps.size(); i++) {
    const auto &step = steps[i];
    for (const auto &output : step.outputs) {
      const auto ins = result.emplace(output, i);
      if (!ins.second) {
        throw BuildError("Multiple rules generate " + output.original());
      }
    }
  }

  return result;
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
  for (size_t i = 0; i < manifest.steps.size(); i++) {
    const auto &step_node = build.step_nodes[i];
    if (step_node.should_build && step_node.dependencies == 0) {
      result.push_back(i);
    }
  }
  return result;
}

/**
 * Recursive helper for computeBuild. Implements the DFS traversal.
 */
void visitStep(
    const OutputFileMap &output_file_map,
    Build &build,
    std::vector<StepIndex> &cycle,
    StepIndex idx) throw(BuildError) {
  auto &step_node = build.step_nodes[idx];
  if (step_node.currently_visited) {
    // TODO(peck): Better error message
    throw BuildError("Cycle detected");
  }

  if (step_node.should_build) {
    // The step has already been processed.
    return;
  }
  step_node.should_build = true;

  step_node.currently_visited = true;
  cycle.push_back(idx);

  const auto &step = manifest.steps[idx];
  const auto process_inputs = [&](const std::vector<Path> &inputs) {
    for (const auto &input : inputs) {
      const auto it = output_file_map.find(input);
      if (it == output_file_map.end()) {
        // This input is not an output of some other build step.
        continue;
      }

      const auto dependency_idx = it->second;
      auto &dependency_node = build.step_nodes[dependency_idx];
      dependency_node.dependents.push_back(step_idx);
      step.dependencies++;

      visitStep(output_file_map, build, cycle, dependency_idx);
    }
  };
  process_inputs(step.inputs);
  process_inputs(step.implicit_inputs);
  // TODO(peck): Handle order-only dependencies

  cycle.pop_back();
  step_node.currently_visited = false;
}

/**
 * Create a Build object suitable for use as a starting point for the build.
 */
Build computeBuild(
    std::vector<StepIndex> &&steps_to_build,
    const OutputFileMap &output_file_map,
    const Manifest &manifest) {
  Build build;
  build.step_nodes.resize(manifest.steps.size());

  std::vector<StepIndex> cycle;
  cycle.reserve(32);  // Guess at largest typical build dependency depth
  for (const auto step_idx : steps_to_build) {
    visitStep(output_file_map, build, cycle, step_idx);
  }

  build.ready_steps = computeReadySteps(build.step_nodes);
  return build;
}

/**
 * The fingerprinting system sometimes asks for a fingerprint of a clean target
 * to be recomputed (this usually happens when the entry is "racily clean" which
 * makes it necessary to hash the file contents to detect if the file is dirty
 * or not). This function takes an Invocations::Entry, recomputes the
 * fingerprints and creates a new Invocations::Entry with fresh fingerprints.
 */
Invocations::Entry recomputeInvocationEntry(
    FileSystem &file_system, const Invocations::Entry &entry) {
  // TODO(peck): Implement me

  return entry;
}

/**
 * Checks if a build step has already been performed and does not need to be
 * run again. This is not purely a read-only action: It uses fingerprints, and
 * if the fingerprint logic wants a fresher fingerprint in the invocation log
 * for the future, isClean provides that.
 */
bool isClean(
    FileSystem &file_system,
    InvocationLog &invocation_log,
    const Invocations &invocations,
    const Hash &step_hash) throw(IoError) {
  const auto it = invocations.find(step_hash);
  if (it == invocations.end()) {
    return false;
  }

  bool should_update = false;
  bool clean = true;
  const auto process_files = [&](
      const std::vector<std::pair<Path, Fingerprint>> &files) {
    for (const auto &file : files) {
      const auto match = fingerprintMatches(
          file_system,
          file.first.original(),
          file.second);
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

  if (clean && should_update) {
    // There is no need to update the invocation log when dirty; it will be
    // updated anyway as part of the build.
    invocation_log.ranCommand(
        step_hash, recomputeInvocationEntry(file_system, entry));
  }

  return clean;
}

StepHashes computeStepHashes(const std::vector<Step> &steps) {
  StepHashes hashes;
  hashes.reserve(steps.size());

  for (const auto &step : steps) {
    hashes.push_back(step.hash());  // TODO(peck): Add Step::hash
  }

  return hashes;
}

/**
 * Before the actual build is performed, this function goes through the build
 * graph and removes steps that don't need to be built because they are already
 * built.
 */
void discardCleanSteps(
    FileSystem &file_system,
    InvocationLog &invocation_log,
    const Invocations &invocations,
    const Manifest &manifest,
    const StepHashes &step_hashes,
    Build &build) throw(IoError) {
  // This function goes through and consumes build.ready_steps. While doing that
  // it adds an element to new_ready_steps for each dirty step that it
  // encounters. When this function's search is over, it replaces
  // build.ready_steps with this list.
  std::vector<StepIndex> new_ready_steps;

  // Memo map of step index => visited. This is to make sure that each step
  // is processed at most once.
  std::vector<bool> visited(manifest.steps.size(), false);

  // This is a BFS search loop. build.ready_steps is the work stack.
  while (!build.ready_steps.empty()) {
    const auto step_idx = build.ready_steps.back();
    ready_steps.pop_back();
    const auto &step = manifest.steps[step_idx];

    if (visited[step_idx]) {
      continue;
    }
    visited[step_idx] = true;

    const auto &step_hash = step_hashes[step_idx];
    if (isClean(file_system, invocation_log, invocations, step_hash)) {
      build.markStepNodeAsDone(step_idx);
    } else {
      new_ready_steps.push_back(step_idx);
    }
  }

  build.ready_steps.swap(new_ready_steps);
}

void deleteTemporaryBuildFile(
    FileSystem &file_system,
    Path path) {
  // TODO(peck): Implement me
}

void mkdirsForPath(
    FileSystem &file_system,
    InvocationLog &invocation_log,
    Path path) {
  // TODO(peck): Implement me
}

void commandDone(
    FileSystem &file_system,
    CommandRunner &command_runner,
    BuildStatus &build_status,
    InvocationLog &invocation_log,
    const Manifest &manifest,
    const StepHashes &step_hashes,
    Build &build,
    StepIndex step_idx,
    CommandRunner::Result &&result) throw(IoError) {
  const auto &step = manifest.steps[step_idx];

  // TODO(peck): Validate that the command did not read a file that is an output
  //   of a target that it does not depend on directly or indirectly.

  deleteTemporaryBuildFile(file_system, step.depfile);
  deleteTemporaryBuildFile(file_system, step.rspfile);
  build_status.finished(step);  // TODO(peck): Make this happen

  switch (result.exit_status) {
  case ExitStatus::SUCCESS:
    // TODO(peck): Write entry in the InvocationLog

    if (false /*TODO(peck):NYI*/ && step.restat && outputsDidNotChange()) {
      // TODO(peck): Mark this step as clean
    } else {
      build.markStepNodeAsDone(step_idx);
    }
    break;
  case ExitStatus::FAILURE:
    // TODO(peck): Implement me
    break;
  case ExitStatus::INTERRUPTED:
    build.interrupted = true;
    break;
  }

  // Feed the command runner with more commands now that this one is finished.
  // TODO(peck): Is it safe to call invoke from within a Command callback?
  enqueueBuildCommands(
      file_system,
      command_runner,
      build_status,
      invocation_log,
      manfiest,
      step_hashes,
      build);
}

bool enqueueBuildCommand(
    FileSystem &file_system,
    CommandRunner &command_runner,
    BuildStatus &build_status,
    InvocationLog &invocation_log,
    const Manifest &manifest,
    const StepHashes &step_hashes,
    Build &build) throw(IoError) {
  if (build.ready_steps.empty() ||
      build.interrupted ||
      !command_runner.canRunMore()) {
    return false;
  }

  const auto step_idx = build.ready_steps.back();
  const auto &step = manifest.steps[step_idx];
  build.ready_steps.pop_back();

  const auto &step_hash = step_hashes[step_idx];
  deleteOldOutputs(invocations, step);  // TODO(peck): Make this happen

  mkdirsForPath(file_system, invocation_log, step.rspfile);
  file_system.writeFile(step.rspfile, step.rspfile_content);

  for (const auto &output : step.outputs) {
    mkdirsForPath(file_system, invocation_log, output);
  }

  // TODO(peck): What about pools?

  build_status.started(step);  // TODO(peck): Make this happen
  command_runner.invoke(
      step.command,
      step.pool_name == "console" ? UseConsole::YES : UseConsole::NO,
      [&file_system, &command_runner, &build_status, &invocation_log, &manifest,
          &build, step_idx](CommandRunner::Result &&result) {
        commandDone(
            file_system,
            command_runner,
            build_status,
            invocation_log,
            manifest,
            step_hashes,
            build,
            step_idx,
            std::move(result));
      });

  return true;
}


bool enqueueBuildCommands(
    FileSystem &file_system,
    CommandRunner &command_runner,
    BuildStatus &build_status,
    InvocationLog &invocation_log,
    const Manifest &manifest,
    const StepHashes &step_hashes,
    Build &build) throw(IoError) {
  while (enqueueBuildCommand(
      file_system,
      command_runner,
      build_status,
      invocation_log,
      manfiest,
      step_hashes,
      build)) {}
}

void build(
    FileSystem &file_system,
    CommandRunner &command_runner,
    BuildStatus &build_status,
    InvocationLog &invocation_log,
    const Manifest &manifest,
    const Invocations &invocations) throw(IoError, BuildError) {
  deleteStaleOutputs();  // TODO(peck): Make this happen

  const auto output_file_map = computeOutputFileMap(manifest.steps);

  auto steps_to_build = computeStepsToBuild(manifest, output_file_map);

  auto build = computeBuild(
      std::move(steps_to_build),
      output_file_map,
      manifest);

  const auto step_hashes = computeStepHashes(manifest.steps);

  discardCleanSteps(
      file_system,
      invocation_log,
      invocations,
      manifest,
      step_hashes,
      build);

  enqueueBuildCommands(
      file_system,
      command_runner,
      build_status,
      invocation_log,
      manfiest,
      step_hashes,
      build);

  while (!command_runner.empty()) {
    if (command_runner.runCommands()) {
      build.interrupted = true;
    }
  }
}

}
