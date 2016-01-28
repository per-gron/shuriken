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

}  // namespace shk
