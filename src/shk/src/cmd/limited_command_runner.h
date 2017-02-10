#pragma once

#include <memory>

#include "cmd/command_runner.h"

namespace shk {

/**
 * Make a CommandRunner that limits the number of commands that can run in
 * parallel based on CPU load / CPU count.
 */
std::unique_ptr<CommandRunner> makeLimitedCommandRunner(
    const std::function<double ()> &get_load_average,
    double max_load_average,
    int parallelism,
    std::unique_ptr<CommandRunner> &&inner);

}  // namespace shk
