#include "manifest/indexed_manifest.h"

namespace shk {
namespace detail {

template <typename Step>
OutputFileMap computeOutputFileMapGeneric(
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

OutputFileMap computeOutputFileMap(
    const std::vector<RawStep> &steps) throw(BuildError) {
  return computeOutputFileMapGeneric(steps);
}

OutputFileMap computeOutputFileMap(
    const std::vector<Step> &steps) throw(BuildError) {
  return computeOutputFileMapGeneric(steps);
}

StepHashes computeStepHashes(const std::vector<RawStep> &steps) {
  StepHashes hashes;
  hashes.reserve(steps.size());

  for (const auto &step : steps) {
    hashes.push_back(step.hash());
  }

  return hashes;
}

}  // namespace detail

namespace {

std::vector<Step> convertStepVector(std::vector<RawStep> &&steps) {
  std::vector<Step> ans;
  ans.reserve(steps.size());

  for (auto &step : steps) {
    ans.emplace_back(std::move(step));
  }

  return ans;
}

}  // anonymous namespace

IndexedManifest::IndexedManifest(RawManifest &&manifest)
    : output_file_map(detail::computeOutputFileMap(manifest.steps)),
      step_hashes(detail::computeStepHashes(manifest.steps)),
      steps(convertStepVector(std::move(manifest.steps))),
      defaults(std::move(manifest.defaults)),
      pools(std::move(manifest.pools)),
      build_dir(std::move(manifest.build_dir)) {}

}  // namespace shk
