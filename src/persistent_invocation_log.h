#pragma once

#include "invocation_log.h"

namespace shk {

/**
 * Create a disk-backed InvocationLog. This is the main InvocationLog
 * implementation.
 */
std::unique_ptr<InvocationLog> makePersistentInvocationLog(
    FileSystem &file_system, const std::string &log_path);

}  // namespace shk
