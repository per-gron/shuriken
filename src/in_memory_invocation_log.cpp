#include "in_memory_invocation_log.h"

namespace shk {

void InMemoryInvocationLog::createdDirectory(const std::string &path) throw(IoError) {
  _created_directories.insert(path);
}

void InMemoryInvocationLog::removedDirectory(const std::string &path) throw(IoError) {
  _created_directories.erase(path);
}

void InMemoryInvocationLog::ranCommand(
    const Hash &build_step_hash, const Entry &entry) throw(IoError) {
  _entries[build_step_hash] = entry;
}

void InMemoryInvocationLog::cleanedCommand(
    const Hash &build_step_hash) throw(IoError) {
  _entries.erase(build_step_hash);
}

Invocations InMemoryInvocationLog::invocations(Paths &paths) const {
  Invocations result;

  for (const auto &dir : _created_directories) {
    result.created_directories.insert(paths.get(dir));
  }

  for (const auto &log_entry : _entries) {
    Invocations::Entry entry;
    const auto files = [&](
        const std::vector<std::pair<std::string, Fingerprint>> &files) {
      std::vector<std::pair<Path, Fingerprint>> result;
      for (const auto &file : files) {
        result.emplace_back(paths.get(file.first), file.second);
      }
      return result;
    };
    entry.output_files = files(log_entry.second.output_files);
    entry.input_files = files(log_entry.second.input_files);
    result.entries[log_entry.first] = entry;
  }

  return result;
}

}  // namespace shk
