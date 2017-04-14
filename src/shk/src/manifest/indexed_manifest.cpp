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
    const std::vector<Step> &steps,
    const PathToStepMap &output_path_map) throw(BuildError) {
  std::vector<StepIndex> result;
  // Assume that all steps are roots until we find some step that has an input
  // that is in a given step's list of outputs. Such steps are not roots.
  std::vector<bool> roots(steps.size(), true);

  for (size_t i = 0; i < steps.size(); i++) {
    for (const auto &input : steps[i].dependencies) {
      const auto it = output_path_map.find(input);
      if (it != output_path_map.end()) {
        roots[it->second] = false;
      }
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

PathToStepMap computeInputPathMap(
    const std::vector<RawStep> &steps) throw(BuildError) {
  PathToStepMap result;

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

Step convertRawStep(RawStep &&raw) {
  Step::Builder builder;
  builder.setHash(raw.hash());

  std::vector<Path> dependencies;
  dependencies.reserve(
      raw.inputs.size() +
      raw.implicit_inputs.size() +
      raw.dependencies.size());

  std::copy(
      raw.inputs.begin(),
      raw.inputs.end(),
      std::back_inserter(dependencies));
  std::copy(
      raw.implicit_inputs.begin(),
      raw.implicit_inputs.end(),
      std::back_inserter(dependencies));
  std::copy(
      raw.dependencies.begin(),
      raw.dependencies.end(),
      std::back_inserter(dependencies));

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

std::vector<Step> convertStepVector(std::vector<RawStep> &&steps) {
  std::vector<Step> ans;
  ans.reserve(steps.size());

  for (auto &step : steps) {
    ans.push_back(convertRawStep(std::move(step)));
  }

  return ans;
}

std::vector<StepIndex> computeStepsToBuildFromPaths(
    const std::vector<Path> &paths,
    const PathToStepMap &output_path_map) throw(BuildError) {
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

}  // anonymous namespace

IndexedManifest::IndexedManifest(RawManifest &&manifest)
    : output_path_map(detail::computeOutputPathMap(manifest.steps)),
      input_path_map(computeInputPathMap(manifest.steps)),
      steps(convertStepVector(std::move(manifest.steps))),
      defaults(computeStepsToBuildFromPaths(
          manifest.defaults, output_path_map)),
      roots(detail::rootSteps(steps, output_path_map)),
      pools(std::move(manifest.pools)),
      build_dir(std::move(manifest.build_dir)) {}

}  // namespace shk
