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
