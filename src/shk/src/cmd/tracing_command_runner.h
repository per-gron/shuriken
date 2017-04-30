// Copyright 2017 Per Grön. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <memory>

#include "cmd/command_runner.h"
#include "cmd/trace_server_handle.h"
#include "fs/file_system.h"
#include "string_view.h"

namespace shk {
namespace detail {

/**
 * Parse a flatbuffer Trace object and add the input files, output files
 * and potentially errors to this Result. If there are errors, exit_status
 * is set accordingly.
 */
void parseTrace(string_view trace, CommandRunner::Result *result);

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
    const std::shared_ptr<TraceServerHandle> &trace_sever_handle,
    FileSystem &file_system,
    std::unique_ptr<CommandRunner> &&command_runner);

}  // namespace shk
