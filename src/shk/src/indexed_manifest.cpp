#include "indexed_manifest.h"

namespace shk {
namespace detail {

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

StepHashes computeStepHashes(const std::vector<Step> &steps) {
  StepHashes hashes;
  hashes.reserve(steps.size());

  for (const auto &step : steps) {
    hashes.push_back(step.hash());
  }

  return hashes;
}

}  // namespace detail

IndexedManifest::IndexedManifest(Manifest &&manifest)
    : output_file_map(detail::computeOutputFileMap(manifest.steps)),
      step_hashes(detail::computeStepHashes(manifest.steps)),
      manifest(std::move(manifest)) {}

}  // namespace shk
