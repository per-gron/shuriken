#pragma once

#include <memory>
#include <unordered_map>

#include "cmd/command_runner.h"

namespace shk {

/**
 * Make a CommandRunner that limits the number of commands that can run in
 * parallel for build pools that have limited capacity.
 *
 * If used together with LimitedCommandRunner, the PooledCommandRunner should
 * be "outside" the LimitedCommandRunner: otherwise commands that are delayed
 * just because of pool limitations will count towards the concurrent commands
 * limit.
 */
std::unique_ptr<CommandRunner> makePooledCommandRunner(
    const std::unordered_map<std::string, int> &pools,
    std::unique_ptr<CommandRunner> &&inner);

}  // namespace shk
