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

#include <catch.hpp>

#include "cmd/limited_command_runner.h"

#include "../dummy_command_runner.h"
#include "../in_memory_file_system.h"
#include "../manifest/step_builder.h"

namespace shk {

TEST_CASE("LimitedCommandRunner") {
  flatbuffers::FlatBufferBuilder builder;
  auto step = StepBuilder().setPoolName("a_pool").build(builder);

  InMemoryFileSystem fs;
  double current_load_average = 0;
  const auto runner = makeLimitedCommandRunner(
      [&]() { return current_load_average; },
      0.5,
      2,
      std::unique_ptr<CommandRunner>(
          new DummyCommandRunner(fs)));

  const auto cmd = DummyCommandRunner::constructCommand({}, {});

  SECTION("ForwardedMethods") {
    CHECK(runner->size() == 0);
    bool callback_called = false;
    runner->invoke(cmd, step, [&](CommandRunner::Result &&) {
      callback_called = true;
    });
    CHECK(runner->size() == 1);
    CHECK(!callback_called);
    CHECK(!runner->runCommands());
    CHECK(callback_called);
    CHECK(runner->size() == 0);
  }

  SECTION("Parallelism") {
    CHECK(runner->canRunMore());
    runner->invoke(cmd, step, [&](CommandRunner::Result &&) {});
    CHECK(runner->canRunMore());
    runner->invoke(cmd, step, [&](CommandRunner::Result &&) {});
    CHECK(!runner->canRunMore());
  }

  SECTION("LoadAverageWhenEmpty") {
    current_load_average = 1;
    CHECK(runner->canRunMore());
  }

  SECTION("LoadAverageWhenEmpty") {
    runner->invoke(cmd, step, [&](CommandRunner::Result &&) {});
    CHECK(runner->canRunMore());
    current_load_average = 0.5;
    CHECK(!runner->canRunMore());
    current_load_average = 0.2;
    CHECK(runner->canRunMore());
  }

}

}  // namespace shk
