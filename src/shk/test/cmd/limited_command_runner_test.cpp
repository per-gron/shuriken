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
