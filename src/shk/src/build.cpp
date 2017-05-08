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

#include "build.h"

#include <assert.h>
#include <atomic>
#include <errno.h>
#include <thread>

#include <util/path_operations.h>

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

std::vector<FileId> outputFileIdsForBuildStep(
    const Invocations &invocations,
    const FingerprintMatchesMemo &fingerprint_matches_memo,
    Step step) {
  if (step.phony() || step.generator()) {
    // Phony steps are never recorded in the invocation log, but they also never
    // have any outputs so it's fine to do nothing here.
    //
    // Generator steps are also not recorded in the invocation log. These steps
    // do have outputs though. The only reason it's okay to return nothing here
    // in this case is that this function is defined as to not return anything
    // in that case. This is the reason why Build::output_files does not contain
    // file ids of generator steps, which in turn is a reason for why normal
    // steps can't depend on generator steps; because then the
    // ignored_dependencies and additional_dependencies calculation that is done
    // when a non-generator build step is finished before writing to the
    // invocation log would not work.
    return {};
  }

  const auto entry_it = invocations.entries.find(step.hash());
  if (entry_it == invocations.entries.end()) {
    // The caller must make sure that the step hash actually exists in the
    // invocations object. If it doesn't, then the step is not clean, and the
    // caller should have made sure that it is before calling this function.
    throw std::runtime_error(
        "internal error: outputFileIdsForBuildStep invoked with invalid step "
        "hash");
  }
  const auto &entry = entry_it->second;

  std::vector<FileId> file_ids;
  file_ids.reserve(entry.output_files.size());

  for (const auto fingerprint_idx : entry.output_files) {
    const auto &matches_result = fingerprint_matches_memo[fingerprint_idx];
    if (!matches_result) {
      throw std::runtime_error(
          "internal error: outputFileIdsForBuildStep invoked with step that is "
          "not included in the build");
    }

    file_ids.push_back(matches_result->file_id);
  }

  return file_ids;
}

std::vector<StepIndex> usedDependencies(
    const std::unordered_map<FileId, StepIndex> &written_files,
    const std::vector<FileId> &input_file_ids) {
  std::vector<StepIndex> used_dependencies;
  for (auto input_file_id : input_file_ids) {
    if (input_file_id.missing()) {
      // This check is not strictly needed since written_files never contains
      // the empty FileId but it saves on unnecessary hash table lookups.
      continue;
    }
    const auto written_files_it = written_files.find(input_file_id);
    if (written_files_it == written_files.end()) {
      continue;
    }

    used_dependencies.push_back(written_files_it->second);
  }

  std::sort(used_dependencies.begin(), used_dependencies.end());
  used_dependencies.erase(
      std::unique(used_dependencies.begin(), used_dependencies.end()),
      used_dependencies.end());

  return used_dependencies;
}

std::pair<std::vector<uint32_t>, std::vector<Hash>>
ignoredAndAdditionalDependencies(
    StepsView steps,
    Step step,
    const std::vector<StepIndex> &used_dependencies) {
  std::vector<uint32_t> ignored_dependencies;
  std::vector<Hash> additional_dependencies;

  auto deps = step.dependencies();
  auto dep_it = deps.begin();
  auto used_dep_it = used_dependencies.begin();
  for (;;) {
    const bool dep_end = dep_it == deps.end();
    const bool used_dep_end = used_dep_it == used_dependencies.end();
    if (dep_end && used_dep_end) {
      break;
    }

    if (!dep_end && !used_dep_end && *dep_it == *used_dep_it) {
      // *dep_it is used, so it's neither ignored nor additional
      ++dep_it;
      ++used_dep_it;
    } else if (used_dep_end || (!dep_end && *dep_it < *used_dep_it)) {
      // *dep_it is not used
      ignored_dependencies.push_back(*dep_it);
      ++dep_it;
    } else {
      // *used_dep_it is not in the step's direct dependency list
      additional_dependencies.push_back(steps[*used_dep_it].hash());
      ++used_dep_it;
    }
  }

  std::sort(additional_dependencies.begin(), additional_dependencies.end());

  return std::make_pair(
      std::move(ignored_dependencies),
      std::move(additional_dependencies));
}

std::pair<std::vector<uint32_t>, std::vector<Hash>>
ignoredAndAdditionalDependencies(
    const std::unordered_map<FileId, StepIndex> &written_files,
    StepsView steps,
    Step step,
    const std::vector<FileId> &input_file_ids) {
  return ignoredAndAdditionalDependencies(
      steps,
      step,
      usedDependencies(written_files, input_file_ids));
}

bool stepIsIgnored(
    const Invocations &invocations,
    const Hash &possibly_ignoring_step_hash,
    StepIndex possibly_ignored_step) {
  const auto invocation_entry_it =
      invocations.entries.find(possibly_ignoring_step_hash);
  if (invocation_entry_it == invocations.entries.end()) {
    return false;
  }
  const auto &entry = invocation_entry_it->second;
  return std::binary_search(
      entry.ignored_dependencies.begin(),
      entry.ignored_dependencies.end(),
      possibly_ignored_step);
}

void Build::markStepNodeAsDone(
    StepIndex step_idx,
    const std::vector<FileId> &output_file_ids,
    bool step_was_skipped) {
  for (const auto &file_id : output_file_ids) {
    if (file_id.missing()) {
      // If file_id.missing(), then it's just zero, and equal to all other
      // missing file ids. It does not make sense to add that to output_files.
      continue;
    }
    if (!output_files.emplace(file_id, step_idx).second) {
      throw BuildError("More than one step wrote to the same file");
    }
  }

  const auto &dependents = step_nodes[step_idx].dependents;
  for (const auto dependent_idx : dependents) {
    auto &dependent = step_nodes[dependent_idx];

    if (!step_was_skipped && dependent.no_direct_dependencies_built) {
      // If this step_was_skipped then no_direct_dependencies_built is not
      // affected. Also, if dependent.no_direct_dependencies_built has already
      // been set to false there is no need to spend time on it since it can
      // only be set to false.

      const auto &dependent_step_hash = _steps[dependent_idx].hash();
      if (!stepIsIgnored(_invocations, dependent_step_hash, step_idx)) {
        // If this (step_idx) step is an ignored dependency for dependent, then
        // this step doesn't count as a direct dependency and the flag doesn't
        // need to be set.
        //
        // (This assumption would not be safe unless Build::construct/visitStep
        // went through and marked additional_dependencies as direct
        // dependencies.)
        dependent.no_direct_dependencies_built = false;
      }
    }

    assert(dependent.dependencies);
    dependent.dependencies--;
    if (dependent.dependencies == 0) {
      ready_steps.push_back(dependent_idx);
    }
  }
}

/**
 * Helper for Build::construct.
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
 * Recursive helper for Build::construct. Implements the DFS traversal.
 */
void visitStep(
    const CompiledManifest &manifest,
    const std::unordered_map<Hash, StepIndex> &step_indices_map,
    const Invocations &invocations,
    Build &build,
    StepIndex idx) throw(BuildError) {
  auto &step_node = build.step_nodes[idx];
  if (step_node.currently_visited) {
    // Dependency cycles should be detected when compiling the manifest; this is
    // just a check to avoid stack overflow in case things go wrong.
    throw BuildError("Dependency cycle");
  }

  if (step_node.should_build) {
    // The step has already been processed.
    return;
  }
  step_node.should_build = true;

  const auto add_dependency = [&](StepIndex dependency_idx) {
    auto &dependency_node = build.step_nodes[dependency_idx];
    dependency_node.dependents.push_back(idx);
    step_node.dependencies++;

    visitStep(
        manifest,
        step_indices_map,
        invocations,
        build,
        dependency_idx);
  };

  step_node.currently_visited = true;

  // Iterate over dependencies declared in the manifest.
  for (const auto &dependency_idx : manifest.steps()[idx].dependencies()) {
    add_dependency(dependency_idx);
  }

  // If the step has an entry in the invocation log, also iterate over
  // additional_dependencies. This would normally be a no-op because the
  // additional_dependencies should always be transitive dependencies of the
  // dependencies in the manifest. However, the handling of
  // StepNode::no_direct_dependencies_built requires this to be added, otherwise
  // these additional dependencies risk to not make no_direct_dependencies_built
  // false because the direct dependency is marked as ignored.
  const auto entry_it = invocations.entries.find(manifest.steps()[idx].hash());
  if (entry_it != invocations.entries.end()) {
    for (const auto &hash : entry_it->second.additional_dependencies) {
      const auto idx_it = step_indices_map.find(hash);
      if (idx_it == step_indices_map.end()) {
        // One of the additional dependencies that were there when this step was
        // built is no longer in the manifest, at least not with the exact same
        // parameters. This means that we can't really know if any of the direct
        // dependencies will be built or not, so be safe and set the flag
        // immediately (this )
        step_node.no_direct_dependencies_built = false;
      } else {
        add_dependency(idx_it->second);
      }
    }
  }

  step_node.currently_visited = false;
}

Build Build::construct(
    const CompiledManifest &manifest,
    const Invocations &invocations,
    size_t failures_allowed,
    std::vector<StepIndex> &&steps_to_build) throw(BuildError) {
  Build build(invocations, manifest.steps());
  build.step_nodes.resize(manifest.steps().size());

  std::unordered_map<Hash, StepIndex> step_indices_map;
  const auto steps = manifest.steps();
  for (StepIndex i = 0; i < steps.size(); i++) {
    step_indices_map[steps[i].hash()] = i;
  }

  for (const auto step_idx : steps_to_build) {
    visitStep(
        manifest,
        step_indices_map,
        invocations,
        build,
        step_idx);
  }

  build.ready_steps = computeReadySteps(build.step_nodes);
  build.remaining_failures = failures_allowed;
  return build;
}

int Build::discardCleanSteps(
    const Invocations &invocations,
    const FingerprintMatchesMemo &fingerprint_matches_memo,
    StepsView steps,
    const CleanSteps &clean_steps) {
  int discarded_steps = 0;

  // This function goes through and consumes build.ready_steps. While doing that
  // it adds an element to new_ready_steps for each dirty step that it
  // encounters. When this function's search is over, it replaces
  // build.ready_steps with this list.
  std::vector<StepIndex> new_ready_steps;

  // Memo map of step index => visited. This is to make sure that each step
  // is processed at most once.
  std::vector<bool> visited(step_nodes.size(), false);

  // This is a BFS search loop. ready_steps is the work stack.
  while (!ready_steps.empty()) {
    const auto step_idx = ready_steps.back();
    ready_steps.pop_back();

    if (visited[step_idx]) {
      continue;
    }
    visited[step_idx] = true;

    const bool phony = steps[step_idx].phony();
    if (clean_steps[step_idx] || phony) {
      if (!phony) {
        discarded_steps++;
      }

      const auto output_file_ids = outputFileIdsForBuildStep(
          invocations,
          fingerprint_matches_memo,
          steps[step_idx]);
      markStepNodeAsDone(step_idx, output_file_ids, /*step_was_skipped:*/true);
    } else {
      new_ready_steps.push_back(step_idx);
    }
  }

  ready_steps.swap(new_ready_steps);

  return discarded_steps;
}

Build::Build(const Invocations &invocations, StepsView steps)
    : _invocations(invocations),
      _steps(steps) {}

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

namespace {

void relogCommand(
    InvocationLog &invocation_log,
    const Invocations &invocations,
    const Invocations::Entry &entry,
    const Hash &step_hash) {
  auto make_files_vector = [&](IndicesView file_indices) {
    std::vector<std::string> files;
    files.reserve(file_indices.size());
    for (const uint32_t file_index : file_indices) {
      files.emplace_back(invocations.fingerprints[file_index].first);
    }
    return files;
  };

  auto output_files = make_files_vector(entry.output_files);
  auto input_files = make_files_vector(entry.input_files);

  std::vector<uint32_t> ignored_dependencies(
      entry.ignored_dependencies.begin(),
      entry.ignored_dependencies.end());

  std::vector<Hash> additional_dependencies(
      entry.additional_dependencies.begin(),
      entry.additional_dependencies.end());

  invocation_log.ranCommand(
      step_hash,
      std::move(output_files),
      invocation_log.fingerprintFiles(output_files),
      std::move(input_files),
      invocation_log.fingerprintFiles(input_files),
      std::move(ignored_dependencies),
      std::move(additional_dependencies));
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
  const auto process_files = [&](IndicesView fingerprints) {
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

}  // anonymous namespace

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
      invocations.fingerprints,
      invocations.fingerprintsFor(entries));
}

FingerprintMatchesMemo computeFingerprintMatchesMemo(
    FileSystem &file_system,
    const std::vector<std::pair<nt_string_view, const Fingerprint &>> &
        fingerprints,
    const std::vector<uint32_t> used_fingerprints) {
  FingerprintMatchesMemo memo(fingerprints.size());

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
        const auto &file = fingerprints[fingerprint_idx];
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
    const Build &build,
    const FingerprintMatchesMemo &fingerprint_matches_memo) throw(IoError) {
  assert(steps.size() == build.step_nodes.size());

  CleanSteps result(build.step_nodes.size(), false);

  for (size_t i = 0; i < build.step_nodes.size(); i++) {
    const auto &step_node = build.step_nodes[i];
    if (!step_node.should_build) {
      continue;
    }
    result[i] = isClean(
        file_system,
        invocation_log,
        fingerprint_matches_memo,
        invocations,
        steps[i]);
  }

  return result;
}

void deleteBuildProduct(
    FileSystem &file_system,
    const Invocations &invocations,
    InvocationLog &invocation_log,
    nt_string_view path) throw(IoError) {
  if (auto error = file_system.unlink(path)) {
    if (error.code() != ENOENT) {
      throw IoError(
          std::string("Failed to unlink build product ") +
          std::string(path) + ": " + error.what(),
          error.code());
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
      if (auto error = file_system.rmdir(dir)) {
        throw error;
      }
      invocation_log.removedDirectory(dir);
    } catch (const IoError &error) {
      if (error.code() == ENOTEMPTY) {
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
  std::vector<std::string> created_dirs;
  IoError error;
  std::tie(created_dirs, error) = mkdirs(file_system, path);
  if (error) {
    throw error;
  }
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

  const auto output_file_ids = outputFileIdsForBuildStep(
      params.invocations,
      params.fingerprint_matches_memo,
      step);
  params.build.markStepNodeAsDone(
      step_idx, output_file_ids, /*step_was_skipped:*/true);
}

void commandDone(
    BuildCommandParameters &params,
    StepIndex step_idx,
    CommandRunner::Result &&result) throw(IoError) {
  const auto step = params.manifest.steps()[step_idx];

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
  std::vector<FileId> output_file_ids;
  for (const auto &output_file : result.output_files) {
    Fingerprint fingerprint;
    FileId file_id;
    std::tie(fingerprint, file_id) =
        params.invocation_log.fingerprint(output_file);

    output_fingerprints.push_back(fingerprint);
    output_file_ids.push_back(file_id);

    // fingerprint.stat.couldAccess() can be false for example for a depfile,
    // which will have already been deleted above.
    if (fingerprint.stat.couldAccess()) {
      auto &written_files = params.build.written_files;
      if (!written_files.emplace(file_id, fingerprint.hash).second) {
        // This is a sanity check, but it is not complete, since it is
        // possible to overwrite a file in a way so that the FileId changes.
        result.exit_status = ExitStatus::FAILURE;
        result.output +=
            "shk: Build step wrote to file that other build step has already "
            "written to: " + output_file + "\n";
      }
    }
  }

  std::vector<Fingerprint> input_fingerprints;
  std::vector<FileId> input_file_ids;
  for (const auto &input_file : result.input_files) {
    Fingerprint fingerprint;
    FileId file_id;
    std::tie(fingerprint, file_id) =
        params.invocation_log.fingerprint(input_file);

    input_fingerprints.push_back(fingerprint);
    input_file_ids.push_back(file_id);
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

      std::vector<uint32_t> ignored_dependencies;
      std::vector<Hash> additional_dependencies;
      std::tie(ignored_dependencies, additional_dependencies) =
          ignoredAndAdditionalDependencies(
              params.build.output_files,
              params.manifest.steps(),
              step,
              input_file_ids);

      params.invocation_log.ranCommand(
          step.hash(),
          std::move(result.output_files),
          std::move(output_fingerprints),
          std::move(result.input_files),
          std::move(input_fingerprints),
          std::move(ignored_dependencies),
          std::move(additional_dependencies));
    }

    params.build.markStepNodeAsDone(
        step_idx, output_file_ids, /*step_was_skipped:*/false);
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
    bool no_direct_dependencies_built,
    const Step &step,
    StepIndex step_idx) {
  if (!clean_steps[step_idx]) {
    // The step was not clean at the start of the build.
    //
    // Technically, we could check if the step has become clean here and return
    // true, but that doesn't seem like a common use case.
    return false;
  }

  if (no_direct_dependencies_built) {
    // If the step was clean at the start of the build, and no direct
    // dependencies have been built, then we know for sure that this step is
    // still clean; there is no need to do any other checks.
    return true;
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
    const auto &original_fingerprint =
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

  if (!step.phony()) {
    params.build_status.stepStarted(step);
    params.build.invoked_commands++;
  }

  if (canSkipBuildCommand(
          params.file_system,
          params.clean_steps,
          params.build.written_files,
          params.invocations,
          params.build.step_nodes[step_idx].no_direct_dependencies_built,
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
    if (auto error = params.file_system.writeFile(
            step.rspfile(), step.rspfileContent())) {
      throw error;
    }
  }

  for (const auto &output_dir : step.outputDirs()) {
    mkdirsAndLog(params.file_system, params.invocation_log, output_dir);
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

  auto build = detail::Build::construct(
      manifest,
      invocations,
      failures_allowed,
      std::move(steps_to_build));

  const auto fingerprint_matches_memo = computeFingerprintMatchesMemo(
      file_system, invocations, manifest.steps(), build);

  const auto clean_steps = detail::computeCleanSteps(
      clock,
      file_system,
      invocation_log,
      invocations,
      manifest.steps(),
      build,
      fingerprint_matches_memo);

  const auto discarded_steps = build.discardCleanSteps(
      invocations,
      fingerprint_matches_memo,
      manifest.steps(),
      clean_steps);

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
      fingerprint_matches_memo,
      build);
  detail::enqueueBuildCommands(params);

  while (!command_runner.empty()) {
    if (command_runner.runCommands()) {
      return BuildResult::INTERRUPTED;
    }
  }

  if (build.remaining_failures == failures_allowed) {
    return params.build.invoked_commands == 0 ?
        BuildResult::NO_WORK_TO_DO :
        BuildResult::SUCCESS;
  } else {
    return BuildResult::FAILURE;
  }
}

}
