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

  std::unordered_map<std::string, DependencyType> inputs;
  for (const auto &file_idx : input_files) {
    const auto &file = fingerprints[file_idx];
    inputs.emplace(
        file.first.original(),
        // ALWAYS because if the file would have been a directory and
        // IGNORE_IF_DIRECTORY, then it wouldn't have been here in the first
        // place.
        DependencyType::ALWAYS);
  }

  ranCommand(
      build_step_hash,
      std::move(outputs),
      std::move(inputs));
}

}  // namespace shk
