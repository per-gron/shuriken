#include "invocation_log.h"

namespace shk {

void InvocationLog::relogCommand(
    const Hash &build_step_hash,
    const std::vector<std::pair<Path, Fingerprint>> &output_files,
    const std::vector<std::pair<Path, Fingerprint>> &input_files) {
  std::unordered_set<std::string> outputs;
  for (const auto &file : output_files) {
    outputs.insert(file.first.original());
  }

  std::unordered_map<std::string, DependencyType> inputs;
  for (const auto &file : input_files) {
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
