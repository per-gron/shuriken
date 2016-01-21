#pragma once

#include <memory>

#include "command_runner.h"

namespace shk {

/**
 * Make a CommandRunner that actually runs the commands. This object does not
 * support dependency tracking; it never reports any input_files, output_files
 * or linting_errors; that needs to be done by other means.
 */
std::unique_ptr<CommandRunner> makeRealCommandRunner();

}  // namespace shk
