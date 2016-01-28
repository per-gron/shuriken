#include "persistent_invocation_log.h"

namespace shk {

std::pair<Invocations, std::string> parsePersistentInvocationLog(
    FileSystem &file_system,
    const std::string &log_path) throw(IoError) {
  // TODO(peck): Implement me
  return std::make_pair(Invocations(), "");
}

/**
 * Create a disk-backed InvocationLog. This is the main InvocationLog
 * implementation. The InvocationLog object provided here (like all other such
 * objects) only provide means to write to the invocation log. Reading happens
 * before, in a separate step.
 */
std::unique_ptr<InvocationLog> openPersistentInvocationLog(
    FileSystem &file_system, const std::string &log_path) throw(IoError) {
  // TODO(peck): Implement me
  return nullptr;
}

/**
 * Overwrite the invocation log file with a new one that contains only the
 * entries of invocations. This invalidates any open persistent InvocationLog
 * object to this path: The old invocation log file is unlinked.
 */
void recompactPersistentInvocationLog(
    FileSystem &file_system,
    const Invocations &invocations,
    const std::string &log_path) throw(IoError) {
  // TODO(peck): Implement me
}

}  // namespace shk
