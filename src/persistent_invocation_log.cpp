#include "persistent_invocation_log.h"

namespace shk {

std::unique_ptr<InvocationLog> makePersistentInvocationLog(
    FileSystem &file_system, const std::string &log_path) throw(IoError) {
  // TODO(peck): Implement me
  return nullptr;
}

}  // namespace shk
