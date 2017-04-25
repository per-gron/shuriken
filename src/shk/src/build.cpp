#include "build.h"

#include <assert.h>
#include <atomic>
#include <errno.h>
#include <thread>

#include "fs/fingerprint.h"
#include "util.h"

namespace shk {

StepIndex interpretPath(
    const CompiledManifest &manifest,
    std::string &&path) throw(BuildError) {
  const bool input = !path.empty() && path[path.size() - 1] == '^';
  if (input) {
    path.resize(path.size() - 1);
  }

  try {
    canonicalizePath(&path);
  } catch (const PathError &error) {
    throw BuildError(
        std::string("Invalid target path: ") + error.what());
  }

  const auto path_list = input ?
      manifest.inputs() :
      manifest.outputs();
  auto step_it = std::lower_bound(
      path_list.begin(),
      path_list.end(),
      std::make_pair(path, 0),
      [&](
          const std::pair<nt_string_view, StepIndex> &a,
          const std::pair<nt_string_view, StepIndex> &b) {
        return a.first < b.first;
      });
  if (step_it != path_list.end() && (*step_it).first == path) {
    return (*step_it).second;
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

std::vector<StepIndex> interpretPaths(
    const CompiledManifest &manifest,
    int argc,
    char *argv[]) throw(BuildError) {
  std::vector<StepIndex> targets;
  for (int i = 0; i < argc; ++i) {
    targets.push_back(interpretPath(manifest, argv[i]));
  }
  return targets;
}

std::vector<StepIndex> computeStepsToBuild(
    const CompiledManifest &manifest,
    int argc,
    char *argv[0]) throw(BuildError) {
  auto specified_outputs = interpretPaths(manifest, argc, argv);
  return detail::computeStepsToBuild(
      manifest, std::move(specified_outputs));
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

std::vector<StepIndex> computeStepsToBuild(
    const CompiledManifest &manifest,
    std::vector<StepIndex> &&specified_steps) throw(BuildError) {
  if (!specified_steps.empty()) {
    return specified_steps;
  } else if (!manifest.defaults().empty()) {
    auto defaults = manifest.defaults();
    std::vector<StepIndex> ans(defaults.size());
    std::copy(defaults.begin(), defaults.end(), ans.begin());
    return ans;
  } else {
    if (manifest.roots().empty() && !manifest.steps().empty()) {
      throw BuildError(
          "Could not determine root nodes of build graph. Cyclic dependency?");
    }
    auto roots = manifest.roots();
    std::vector<StepIndex> ans(roots.size());
    std::copy(roots.begin(), roots.end(), ans.begin());
    return ans;
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

/**
 * Recursive helper for computeBuild. Implements the DFS traversal.
 */
void visitStep(
    const CompiledManifest &manifest,
    Build &build,
    StepIndex idx) throw(BuildError) {
  auto &step_node = build.step_nodes[idx];
  if (step_node.currently_visited) {
    throw BuildError("Dependency cycle");
  }

  if (step_node.should_build) {
    // The step has already been processed.
    return;
  }
  step_node.should_build = true;

  step_node.currently_visited = true;
  for (const auto &dependency_idx : manifest.steps()[idx].dependencies()) {
    auto &dependency_node = build.step_nodes[dependency_idx];
    dependency_node.dependents.push_back(idx);
    step_node.dependencies++;

    visitStep(
        manifest,
        build,
        dependency_idx);
  }
  step_node.currently_visited = false;
}

Build computeBuild(
    const CompiledManifest &manifest,
    size_t failures_allowed,
    std::vector<StepIndex> &&steps_to_build) throw(BuildError) {
  Build build;
  build.step_nodes.resize(manifest.steps().size());

  for (const auto step_idx : steps_to_build) {
    visitStep(
        manifest,
        build,
        step_idx);
  }

  build.ready_steps = computeReadySteps(build.step_nodes);
  build.remaining_failures = failures_allowed;
  return build;
}

namespace {

void relogCommand(
    InvocationLog &invocation_log,
    const Invocations &invocations,
    const Invocations::Entry &entry,
    const Hash &step_hash) {
  auto make_files_vector = [&](const std::vector<uint32_t> &file_indices) {
    std::vector<std::string> files;
    files.reserve(file_indices.size());
    for (const uint32_t file_index : file_indices) {
      files.emplace_back(invocations.fingerprints[file_index].first);
    }
    return files;
  };

  auto output_files = make_files_vector(entry.output_files);
  auto input_files = make_files_vector(entry.input_files);

  invocation_log.ranCommand(
      step_hash,
      std::move(output_files),
      invocation_log.fingerprintFiles(output_files),
      std::move(input_files),
      invocation_log.fingerprintFiles(input_files));
}

bool generatorStepIsClean(FileSystem &file_system, Step step) throw(IoError) {
  if (step.generatorInputs().empty() || step.generatorOutputs().empty()) {
    // Nothing to check here.
    return true;
  }

  auto input_mtime = CompiledManifest::maxMtime(
      file_system, step.generatorInputs());
  auto output_mtime = CompiledManifest::minMtime(
      file_system, step.generatorOutputs());

  // Use <= when comparing the times because happens (CMake does this) that
  // it generates some files, like CMakeCache.txt, the same second as it
  // generates the build.ninja file, which would make it dirty if < was used.
  //
  // This is technically racy. But using only mtimes is arguably not
  // particularly correct in the first place. If this does the wrong thing, the
  // build might fail, but at least the build steps if they are cached won't
  // save information that makes the cache wrong.
  return input_mtime && output_mtime && *input_mtime <= *output_mtime;
}

bool nonGeneratorStepIsClean(
    InvocationLog &invocation_log,
    const FingerprintMatchesMemo &fingerprint_matches_memo,
    const Invocations &invocations,
    Step step) throw(IoError) {
  const auto &step_hash = step.hash();
  const auto it = invocations.entries.find(step_hash);
  if (it == invocations.entries.end()) {
    return false;
  }

  bool should_update = false;
  bool clean = true;
  const auto process_files = [&](const std::vector<uint32_t> &fingerprints) {
    for (const auto fingerprint_idx : fingerprints) {
      if (!clean) {
        // There is no need to do any further processing at this point. Because
        // !clean, the command will not be relogged, and by now we already know
        // that the return value of this function will be false, because no
        // fingerprint can make a dirty build step clean.
        return;
      }

      const auto match = fingerprint_matches_memo[fingerprint_idx];
      assert(match);
      if (!match->clean) {
        clean = false;
      }
      if (match->should_update) {
        should_update = true;
      }
    }
  };
  const auto &entry = it->second;
  process_files(entry.output_files);
  process_files(entry.input_files);

  if (should_update && clean) {
    // There is no need to update the invocation log when dirty; it will be
    // updated anyway as part of the build. Also, updating the invocation log
    // when dirty will fingerprint it and effectively mark it as clean, which
    // is not the intention here.
    relogCommand(invocation_log, invocations, entry, step_hash);
  }

  return clean;
}

FingerprintMatchesMemo computeFingerprintMatchesMemo(
    FileSystem &file_system,
    const Invocations &invocations,
    StepsView steps,
    const Build &build) {
  std::vector<const Invocations::Entry *> entries;
  for (size_t i = 0; i < build.step_nodes.size(); i++) {
    const auto &step_node = build.step_nodes[i];
    if (!step_node.should_build) {
      continue;
    }
    const auto entry_it = invocations.entries.find(steps[i].hash());
    if (entry_it == invocations.entries.end()) {
      continue;
    }
    entries.push_back(&entry_it->second);
  }

  return detail::computeFingerprintMatchesMemo(
      file_system,
      invocations,
      invocations.fingerprintsFor(entries));
}

}  // anonymous namespace

FingerprintMatchesMemo computeFingerprintMatchesMemo(
    FileSystem &file_system,
    const Invocations &invocations,
    const std::vector<uint32_t> used_fingerprints) {
  FingerprintMatchesMemo memo(invocations.fingerprints.size());

  const int num_threads = 4;  // Does not scale very well with number of threads
  std::atomic<int> next_used_fingerprints_idx(0);
  std::vector<std::thread> threads;
  for (int i = 0; i < num_threads; i++) {
    threads.emplace_back([&] {
      for (;;) {
        int used_fingerprints_idx = next_used_fingerprints_idx++;
        if (used_fingerprints_idx >= used_fingerprints.size()) {
          break;
        }
        auto fingerprint_idx = used_fingerprints[used_fingerprints_idx];
        const auto &file = invocations.fingerprints[fingerprint_idx];
        memo[fingerprint_idx] =
            fingerprintMatches(file_system, file.first, file.second);
      }
    });
  }
  for (auto &thread : threads) {
    thread.join();
  }

  return memo;
}

bool isClean(
    FileSystem &file_system,
    InvocationLog &invocation_log,
    const FingerprintMatchesMemo &fingerprint_matches_memo,
    const Invocations &invocations,
    Step step) throw(IoError) {
  if (step.generator()) {
    return generatorStepIsClean(file_system, step);
  } else {
    return nonGeneratorStepIsClean(
        invocation_log, fingerprint_matches_memo, invocations, step);
  }
}

CleanSteps computeCleanSteps(
    const Clock &clock,
    FileSystem &file_system,
    InvocationLog &invocation_log,
    const Invocations &invocations,
    StepsView steps,
    const Build &build) throw(IoError) {
  assert(steps.size() == build.step_nodes.size());

  CleanSteps result(build.step_nodes.size(), false);

  auto fingerprint_memo = computeFingerprintMatchesMemo(
      file_system, invocations, steps, build);

  for (size_t i = 0; i < build.step_nodes.size(); i++) {
    const auto &step_node = build.step_nodes[i];
    if (!step_node.should_build) {
      continue;
    }
    result[i] = isClean(
        file_system,
        invocation_log,
        fingerprint_memo,
        invocations,
        steps[i]);
  }

  return result;
}

int discardCleanSteps(
    StepsView steps,
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
    nt_string_view path) throw(IoError) {
  try {
    file_system.unlink(path);
  } catch (const IoError &error) {
    if (error.code != ENOENT) {
      throw IoError(
          std::string("Failed to unlink build product ") +
          std::string(path) + ": " + error.what(),
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

    const auto stat = file_system.lstat(dir);
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

void mkdirsAndLog(
    FileSystem &file_system,
    InvocationLog &invocation_log,
    nt_string_view path) throw(IoError) {
  const auto created_dirs = mkdirs(file_system, path);
  for (const auto &path : created_dirs) {
    invocation_log.createdDirectory(path);
  }
}

void enqueueBuildCommands(BuildCommandParameters &params) throw(IoError);

void commandBypassed(
    BuildCommandParameters &params,
    StepIndex step_idx) throw(IoError) {
  const auto &step = params.manifest.steps()[step_idx];

  // commandBypassed should not be called with phony build steps. This check is
  // here just to be sure.
  if (!step.phony()) {
    params.build_status.stepFinished(
        step,
        true,
        /* command output: */"");
  }

  markStepNodeAsDone(params.build, step_idx);
}

void commandDone(
    BuildCommandParameters &params,
    StepIndex step_idx,
    CommandRunner::Result &&result) throw(IoError) {
  const auto &step = params.manifest.steps()[step_idx];

  if (!step.depfile().empty()) {
    deleteBuildProduct(
        params.file_system,
        params.invocations,
        params.invocation_log,
        std::string(step.depfile()));
  }
  if (!step.rspfile().empty() && result.exit_status != ExitStatus::FAILURE) {
    deleteBuildProduct(
        params.file_system,
        params.invocations,
        params.invocation_log,
        std::string(step.rspfile()));
  }

  std::vector<Fingerprint> output_fingerprints;
  for (const auto &output_file : result.output_files) {
    Fingerprint fingerprint;
    FileId file_id;
    std::tie(fingerprint, file_id) =
        params.invocation_log.fingerprint(output_file);

    output_fingerprints.push_back(fingerprint);

    // fingerprint.stat.couldAccess() can be false for example for a depfile,
    // which will have already been deleted above.
    if (fingerprint.stat.couldAccess()) {
      if (!params.written_files.emplace(file_id, fingerprint.hash).second) {
        // This is a sanity check, but it is not complete, since it is
        // possible to overwrite a file in a way so that the FileId changes.
        result.exit_status = ExitStatus::FAILURE;
        result.output +=
            "shk: Build step wrote to file that other build step has already "
            "written to: " + output_file + "\n";
      }
    }
  }

  if (!step.phony()) {
    params.build_status.stepFinished(
        step,
        result.exit_status == ExitStatus::SUCCESS,
        result.output);
  }

  switch (result.exit_status) {
  case ExitStatus::SUCCESS:
    if (!isConsolePool(step.poolName()) && !step.phony()) {
      // The console pool gives the command access to stdin which is clearly not
      // a deterministic source. Because of this, steps using the console pool
      // are never counted as clean.
      //
      // Phony steps should also not be logged. There is nothing to log then.
      // More importantly though is that logging an empty entry for it will
      // cause the next build to believe that this step has no inputs so it will
      // immediately report the step as clean regardless of what it depends on.

      params.invocation_log.ranCommand(
          params.manifest.steps()[step_idx].hash(),
          std::move(result.output_files),
          params.invocation_log.fingerprintFiles(result.output_files),
          std::move(result.input_files),
          params.invocation_log.fingerprintFiles(result.input_files));
    }

    markStepNodeAsDone(params.build, step_idx);
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
        output.first);
  }
}

bool canSkipBuildCommand(
    FileSystem &file_system,
    const CleanSteps &clean_steps,
    const std::unordered_map<FileId, Hash> &written_files,
    const Invocations &invocations,
    const Step &step,
    StepIndex step_idx) {
  if (!clean_steps[step_idx]) {
    // The step was not clean at the start of the build.
    //
    // Technically, we could check if the step has become clean here and return
    // true, but that doesn't seem like a common use case.
    return false;
  }

  const auto invocation_entry_it = invocations.entries.find(step.hash());
  if (invocation_entry_it == invocations.entries.end()) {
    // Should not happen, but if we do get here it means the step is dirty so
    // we can't skip.
    return false;
  }
  const auto &invocation_entry = invocation_entry_it->second;

  // There is no need to process entry.output_files; we know that they were
  // clean at the start of the build (otherwise we would have returned early)
  // and we know that there are checks that verify that each file is written
  // to by only one step. If this build command is skipped and some other
  // build command wrote to the outputs too, the build will fail anyway.
  for (const auto fingerprint_idx : invocation_entry.input_files) {
    const auto &path =
        invocations.fingerprints[fingerprint_idx].first;
    const auto original_fingerprint =
        invocations.fingerprints[fingerprint_idx].second;

    const auto new_stat = file_system.lstat(path);
    const auto written_file_it = written_files.find(FileId(new_stat));
    if (written_file_it == written_files.end()) {
      continue;
    }
    const auto &new_hash = written_file_it->second;

    if (!fingerprintMatches(original_fingerprint, new_stat, new_hash)) {
      return false;
    }
  }

  return true;
}

bool enqueueBuildCommand(BuildCommandParameters &params) throw(IoError) {
  if (params.build.ready_steps.empty() ||
      !params.command_runner.canRunMore() ||
      params.build.remaining_failures == 0) {
    return false;
  }

  const auto step_idx = params.build.ready_steps.back();
  const auto &step = params.manifest.steps()[step_idx];
  params.build.ready_steps.pop_back();

  if (canSkipBuildCommand(
          params.file_system,
          params.clean_steps,
          params.written_files,
          params.invocations,
          step,
          step_idx)) {
    commandBypassed(params, step_idx);
    return true;
  }

  deleteOldOutputs(
      params.file_system,
      params.invocations,
      params.invocation_log,
      step.hash());

  if (!step.rspfile().empty()) {
    mkdirsAndLog(
        params.file_system,
        params.invocation_log,
        dirname(step.rspfile()));
    params.file_system.writeFile(step.rspfile(), step.rspfileContent());
  }

  for (const auto &output_dir : step.outputDirs()) {
    mkdirsAndLog(params.file_system, params.invocation_log, output_dir);
  }

  if (!step.phony()) {
    params.build_status.stepStarted(step);
    params.invoked_commands++;
  }
  params.command_runner.invoke(
      step.command(),
      step,
      [&params, step_idx](CommandRunner::Result &&result) {
        commandDone(params, step_idx, std::move(result));
      });

  return true;
}


void enqueueBuildCommands(BuildCommandParameters &params) throw(IoError) {
  while (enqueueBuildCommand(params)) {}
}

int countStepsToBuild(StepsView steps, const Build &build) {
  int step_count = 0;

  assert(steps.size() == build.step_nodes.size());
  for (size_t i = 0; i < steps.size(); i++) {
    const auto &step_node = build.step_nodes[i];
    const auto step = steps[i];
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
    StepsView steps,
    const Invocations &invocations) throw(IoError) {
  std::unordered_set<Hash> step_hashes_set;
  step_hashes_set.reserve(steps.size());
  for (const auto step : steps) {
    step_hashes_set.insert(step.hash());
  }

  for (const auto &entry : invocations.entries) {
    if (step_hashes_set.count(entry.first) == 0) {
      for (const auto output_file_idx : entry.second.output_files) {
        const auto &output_file = invocations.fingerprints[output_file_idx];
        detail::deleteBuildProduct(
            file_system,
            invocations,
            invocation_log,
            output_file.first);
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
    std::vector<StepIndex> &&specified_steps,
    const CompiledManifest &manifest,
    const Invocations &invocations) throw(IoError, BuildError) {

  auto steps_to_build = detail::computeStepsToBuild(
      manifest, std::move(specified_steps));

  auto build = detail::computeBuild(
      manifest,
      failures_allowed,
      std::move(steps_to_build));

  const auto clean_steps = detail::computeCleanSteps(
      clock,
      file_system,
      invocation_log,
      invocations,
      manifest.steps(),
      build);

  const auto discarded_steps = detail::discardCleanSteps(
      manifest.steps(), clean_steps, build);

  const auto build_status = make_build_status(
      countStepsToBuild(manifest.steps(), build) - discarded_steps);

  detail::BuildCommandParameters params(
      clock,
      file_system,
      command_runner,
      *build_status,
      invocation_log,
      invocations,
      clean_steps,
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
