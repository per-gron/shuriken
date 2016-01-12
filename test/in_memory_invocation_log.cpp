#include "in_memory_invocation_log.h"

namespace shk {

void InMemoryInvocationLog::createdDirectory(const Path &path) throw(IoError) {
  _invocations.created_directories.insert(path);
}

void InMemoryInvocationLog::removedDirectory(const Path &path) throw(IoError) {
  _invocations.created_directories.erase(path);
}

void InMemoryInvocationLog::ranCommand(
    const Hash &build_step_hash,
    const Invocations::Entry &entry) throw(IoError) {
  _invocations.entries[build_step_hash] = entry;
}

void InMemoryInvocationLog::cleanedCommand(
    const Hash &build_step_hash) throw(IoError) {
  _invocations.entries.erase(build_step_hash);
}

}  // namespace shk
