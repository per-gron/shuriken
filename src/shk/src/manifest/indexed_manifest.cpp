#include "manifest/indexed_manifest.h"

namespace shk {
namespace detail {

PathToStepMap computeOutputPathMap(
    const std::vector<RawStep> &steps) throw(BuildError) {
  PathToStepMap result;

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

std::vector<StepIndex> rootSteps(
    const std::vector<Step> &steps) throw(BuildError) {
  std::vector<StepIndex> result;
  // Assume that all steps are roots until we find some step that has an input
  // that is in a given step's list of outputs. Such steps are not roots.
  std::vector<bool> roots(steps.size(), true);

  for (size_t i = 0; i < steps.size(); i++) {
    for (const auto dependency_idx : steps[i].dependencies()) {
      roots[dependency_idx] = false;
    }
  }

  for (size_t i = 0; i < steps.size(); i++) {
    if (roots[i]) {
      result.push_back(i);
    }
  }

  return result;
}

std::string cycleErrorMessage(const std::vector<Path> &cycle) {
  if (cycle.empty()) {
    // There can't be a cycle without any nodes. Then it's not a cycle...
    return "[internal error]";
  }

  std::string error;
  for (const auto &path : cycle) {
    error += path.original() + " -> ";
  }
  error += cycle.front().original();
  return error;
}

}  // namespace detail

namespace {

detail::PathToStepMap computeInputPathMap(
    const std::vector<RawStep> &steps) throw(BuildError) {
  detail::PathToStepMap result;

  auto process = [&](int idx, const std::vector<Path> &paths) {
    for (const auto path : paths) {
      result.emplace(path, idx);
    }
  };

  for (size_t i = 0; i < steps.size(); i++) {
    const auto &step = steps[i];

    process(i, step.inputs);
    process(i, step.implicit_inputs);
    process(i, step.dependencies);
  }

  return result;
}

PathToStepList computePathList(
    const detail::PathToStepMap &path_map) {
  PathToStepList result;

  for (const auto &path_pair : path_map) {
    auto path = path_pair.first.original();
    try {
      canonicalizePath(&path);
    } catch (const PathError &) {
      continue;
    }
    result.emplace_back(std::move(path), path_pair.second);
  }

  std::sort(result.begin(), result.end());

  return result;
}

Step convertRawStep(
    const detail::PathToStepMap &output_path_map,
    RawStep &&raw) {
  Step::Builder builder;
  builder.setHash(raw.hash());

  std::vector<StepIndex> dependencies;
  const auto process_inputs = [&](
      const std::vector<Path> &paths) {
    for (const auto &path : paths) {
      const auto it = output_path_map.find(path);
      if (it != output_path_map.end()) {
        dependencies.push_back(it->second);
      }
    }
  };
  process_inputs(raw.inputs);
  process_inputs(raw.implicit_inputs);
  process_inputs(raw.dependencies);

  builder.setDependencies(std::move(dependencies));

  std::unordered_set<std::string> output_dirs_set;
  for (const auto &output : raw.outputs) {
    output_dirs_set.insert(dirname(output.original()));
  }
  std::vector<std::string> output_dirs;
  output_dirs.reserve(output_dirs_set.size());
  for (auto &output : output_dirs_set) {
    if (output == ".") {
      continue;
    }
    output_dirs.push_back(std::move(output));
  }
  builder.setOutputDirs(std::move(output_dirs));

  builder.setPoolName(std::move(raw.pool_name));
  builder.setCommand(std::move(raw.command));
  builder.setDescription(std::move(raw.description));
  builder.setGenerator(std::move(raw.generator));
  builder.setDepfile(std::move(raw.depfile));
  builder.setRspfile(std::move(raw.rspfile));
  builder.setRspfileContent(std::move(raw.rspfile_content));
  return builder.build();
}

std::vector<Step> convertStepVector(
    const detail::PathToStepMap &output_path_map,
    std::vector<RawStep> &&steps) {
  std::vector<Step> ans;
  ans.reserve(steps.size());

  for (auto &step : steps) {
    ans.push_back(convertRawStep(output_path_map, std::move(step)));
  }

  return ans;
}

std::vector<StepIndex> computeStepsToBuildFromPaths(
    const std::vector<Path> &paths,
    const detail::PathToStepMap &output_path_map) throw(BuildError) {
  std::vector<StepIndex> result;
  for (const auto &default_path : paths) {
    const auto it = output_path_map.find(default_path);
    if (it == output_path_map.end()) {
      throw BuildError(
          "Specified target does not exist: " + default_path.original());
    }
    // This may result in duplicate values in result, which is ok
    result.push_back(it->second);
  }
  return result;
}

bool hasDependencyCycle(
    const IndexedManifest &manifest,
    const detail::PathToStepMap &output_path_map,
    const std::vector<RawStep> &raw_steps,
    std::vector<bool> &currently_visited,
    std::vector<bool> &already_visited,
    std::vector<Path> &cycle_paths,
    StepIndex idx,
    std::string *cycle) {
  if (currently_visited[idx]) {
    *cycle = detail::cycleErrorMessage(cycle_paths);
    return true;
  }

  if (already_visited[idx]) {
    // The step has already been processed.
    return false;
  }
  already_visited[idx] = true;

  bool found_cycle = false;
  const auto process_inputs = [&](const std::vector<Path> &inputs) {
    if (found_cycle) {
      return;
    }

    for (const auto &input : inputs) {
      const auto it = output_path_map.find(input);
      if (it == output_path_map.end()) {
        // This input is not an output of some other build step.
        continue;
      }

      const auto dependency_idx = it->second;

      cycle_paths.push_back(input);
      if (hasDependencyCycle(
              manifest,
              output_path_map,
              raw_steps,
              currently_visited,
              already_visited,
              cycle_paths,
              dependency_idx,
              cycle)) {
        found_cycle = true;
        return;
      }
      cycle_paths.pop_back();
    }
  };

  currently_visited[idx] = true;
  process_inputs(raw_steps[idx].inputs);
  process_inputs(raw_steps[idx].implicit_inputs);
  process_inputs(raw_steps[idx].dependencies);
  currently_visited[idx] = false;

  return found_cycle;
}

bool hasDependencyCycle(
    const IndexedManifest &indexed_manifest,
    const detail::PathToStepMap &output_path_map,
    const std::vector<RawStep> &raw_steps,
    std::string *cycle) {
  std::vector<bool> currently_visited(indexed_manifest.steps.size());
  std::vector<bool> already_visited(indexed_manifest.steps.size());
  std::vector<Path> cycle_paths;
  cycle_paths.reserve(32);  // Guess at largest typical build dependency depth

  for (StepIndex idx = 0; idx < indexed_manifest.steps.size(); idx++) {
    if (hasDependencyCycle(
            indexed_manifest,
            output_path_map,
            raw_steps,
            currently_visited,
            already_visited,
            cycle_paths,
            idx,
            cycle)) {
      return true;
    }
  }

  return false;
}

StepIndex getManifestStep(
    const detail::PathToStepMap &output_path_map,
    Path manifest_path) {
  auto step_it = output_path_map.find(manifest_path);
  if (step_it == output_path_map.end()) {
    return -1;
  } else {
    return step_it->second;
  }
}

}  // anonymous namespace

IndexedManifest::IndexedManifest(
    Path manifest_path,
    RawManifest &&manifest)
    : IndexedManifest(
          detail::computeOutputPathMap(manifest.steps),
          manifest_path,
          std::move(manifest)) {}

IndexedManifest::IndexedManifest(
    const detail::PathToStepMap &output_path_map,
    Path manifest_path,
    RawManifest &&manifest)
    : outputs(computePathList(output_path_map)),
      inputs(computePathList(computeInputPathMap(manifest.steps))),
      steps(convertStepVector(output_path_map, std::move(manifest.steps))),
      defaults(computeStepsToBuildFromPaths(
          manifest.defaults, output_path_map)),
      roots(detail::rootSteps(steps)),
      pools(std::move(manifest.pools)),
      build_dir(std::move(manifest.build_dir)),
      manifest_step(
          getManifestStep(output_path_map, manifest_path)) {
  hasDependencyCycle(
      *this,
      output_path_map,
      manifest.steps,
      &dependency_cycle);
}

}  // namespace shk
