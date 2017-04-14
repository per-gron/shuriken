#include "log/invocation_log.h"

namespace shk {

void InvocationLog::relogCommand(
    const Hash &build_step_hash,
    const std::vector<std::pair<std::string, Fingerprint>> &fingerprints,
    const std::vector<size_t> &output_files,
    const std::vector<size_t> &input_files) {
  std::vector<std::string> output_paths;
  std::vector<Fingerprint> output_fingerprints;
  for (const auto &file_idx : output_files) {
    const auto &file = fingerprints[file_idx];
    output_paths.push_back(file.first);
    output_fingerprints.push_back(file.second);
  }

  std::vector<std::string> input_paths;
  std::vector<Fingerprint> input_fingerprints;
  for (const auto &file_idx : input_files) {
    const auto &file = fingerprints[file_idx];
    input_paths.push_back(file.first);
    input_fingerprints.push_back(file.second);
  }

  ranCommand(
      build_step_hash,
      std::move(output_paths),
      std::move(output_fingerprints),
      std::move(input_paths),
      std::move(input_fingerprints));
}

std::vector<Fingerprint> InvocationLog::fingerprintFiles(
    const std::vector<std::string> &files) {
  std::vector<Fingerprint> ans;
  ans.reserve(files.size());
  for (const auto &file : files) {
    ans.push_back(fingerprint(file).first);
  }
  return ans;
}

}  // namespace shk
