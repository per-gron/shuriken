#pragma once

#include <memory>

#include "cmd/command_runner.h"
#include "cmd/trace_server_handle.h"
#include "fs/file_system.h"

namespace shk {
namespace detail {

/**
 * Parse a flatbuffer Trace object and add the input files, output files
 * and potentially errors to this Result. If there are errors, exit_status
 * is set accordingly.
 */
void parseTrace(StringPiece trace_slice, CommandRunner::Result *result);

}  // namespace detail

/**
 * Make a CommandRunner that uses another CommandRunner to actually run
 * commands. This CommandRunner will trace reads and writes and perform
 * other verifications that CommandRunners should do.
 *
 * The inner CommandRunner should not perform any linting or dependency
 * tracking; that might be overwritten by this object.
 */
std::unique_ptr<CommandRunner> makeTracingCommandRunner(
    std::unique_ptr<TraceServerHandle> &&trace_sever_handle,
    FileSystem &file_system,
    std::unique_ptr<CommandRunner> &&command_runner);

}  // namespace shk
