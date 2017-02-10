#pragma once

#include <memory>

#include "cmd/command_runner.h"

namespace shk {

/**
 * Make a CommandRunner that doesn't run any commands. It just responds with
 * success.
 */
std::unique_ptr<CommandRunner> makeDryRunCommandRunner();

}  // namespace shk
