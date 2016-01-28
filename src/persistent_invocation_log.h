#pragma once

#include "invocation_log.h"
#include "invocations.h"

namespace shk {

/**
 * Parse an invocation log at a given path into an Invocations object.
 *
 * @return A pair of the parsed Invocations and a string with a potential
 *     warning. This string is empty when there is no warning.
 */
std::pair<Invocations, std::string> parsePersistentInvocationLog(
    FileSystem &file_system,
    const std::string &log_path) throw(IoError);

/**
 * Create a disk-backed InvocationLog. This is the main InvocationLog
 * implementation. The InvocationLog object provided here (like all other such
 * objects) only provide means to write to the invocation log. Reading happens
 * before, in a separate step.
 */
std::unique_ptr<InvocationLog> openPersistentInvocationLog(
    FileSystem &file_system, const std::string &log_path) throw(IoError);

/**
 * Overwrite the invocation log file with a new one that contains only the
 * entries of invocations. This invalidates any open persistent InvocationLog
 * object to this path: The old invocation log file is unlinked.
 */
void recompactPersistentInvocationLog(
    FileSystem &file_system,
    const Invocations &invocations,
    const std::string &log_path) throw(IoError);

}  // namespace shk
