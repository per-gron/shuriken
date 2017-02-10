#pragma once

#include <memory>

#include "cmd/command_runner.h"

namespace shk {

/**
 * Make a CommandRunner that actually runs the commands. This object does not
 * support dependency tracking; it never reports any input_files or
 * output_files; that needs to be done by other means.
 */
std::unique_ptr<CommandRunner> makeRealCommandRunner();

}  // namespace shk
