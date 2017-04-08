#include "log/invocation_log.h"

namespace shk {

void InvocationLog::relogCommand(
    const Hash &build_step_hash,
    const std::vector<std::pair<Path, Fingerprint>> &fingerprints,
    const std::vector<size_t> &output_files,
    const std::vector<size_t> &input_files) {
  std::unordered_set<std::string> outputs;
  for (const auto &file_idx : output_files) {
    const auto &file = fingerprints[file_idx];
    outputs.insert(file.first.original());
  }

  std::unordered_set<std::string> inputs;
  for (const auto &file_idx : input_files) {
    const auto &file = fingerprints[file_idx];
    inputs.insert(file.first.original());
  }

  ranCommand(
      build_step_hash,
      std::move(outputs),
      std::move(inputs));
}

}  // namespace shk
