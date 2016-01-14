#include <catch.hpp>
#include <rapidcheck/catch.h>

#include "dummy_command_runner.h"
#include "generators.h"
#include "in_memory_file_system.h"

namespace shk {

TEST_CASE("DummyCommandRunner") {
  rc::prop("splitCommand of constructCommand should be an identity transformation", []() {
    const auto paths = std::make_shared<Paths>();
    const auto in_inputs = *gen::pathVector(paths);
    const auto in_outputs = *gen::pathVector(paths);

    const auto command = DummyCommandRunner::constructCommand(in_inputs, in_outputs);

    std::vector<Path> out_inputs;
    std::vector<Path> out_outputs;
    std::tie(out_inputs, out_outputs) = detail::splitCommand(*paths, command);

    RC_ASSERT(out_inputs == in_inputs);
    RC_ASSERT(out_outputs == out_outputs);
  });

  Paths paths;
  InMemoryFileSystem file_system(paths);
  DummyCommandRunner runner(paths, file_system);

  SECTION("initially empty") {
    CHECK(runner.empty());
  }

  SECTION("runCommands when empty") {
    runner.runCommands();
  }

  SECTION("runCommand") {
    SECTION("empty command should do nothing") {
      const auto empty_file_system = file_system;
      const auto empty_command = DummyCommandRunner::constructCommand({}, {});
      const auto result = detail::runCommand(paths, file_system, empty_command);

      CHECK(result.return_code == 0);
      CHECK(empty_file_system == file_system);
    }

    SECTION("command should read input files") {
      const auto path = paths.get("abc");
      const auto command = DummyCommandRunner::constructCommand({ path }, {});

      // Should fail because it should try to read a missing file
      const auto result = detail::runCommand(paths, file_system, command);
      CHECK(result.return_code != 0);

      file_system.open(path, "w");  // Create the file
      // Should now not fail anymore
      const auto second_result = detail::runCommand(paths, file_system, command);
      CHECK(second_result.return_code == 0);
    }

    SECTION("command should write output files") {
      const auto path = paths.get("abc");
      const auto command = DummyCommandRunner::constructCommand({}, { path });

      const auto result = detail::runCommand(paths, file_system, command);
      CHECK(result.return_code == 0);

      CHECK(file_system.stat(path).result == 0);  // Output file should have been created
    }
  }

  SECTION("invoke") {
    // These are small sanity checks for this function. It is more thoroughly
    // tested by the checkCommand property based test.

    SECTION("should create output file") {
      const auto path = paths.get("abc");
      const auto command = DummyCommandRunner::constructCommand({}, { path });

      runner.invoke(command, CommandRunner::noopCallback);
      while (!runner.empty()) {
        runner.runCommands();
      }

      CHECK(file_system.stat(path).result == 0);
    }

    SECTION("should fail with missing input") {
      const auto path = paths.get("abc");
      const auto command = DummyCommandRunner::constructCommand({ path }, {});

      int return_code = 0;
      runner.invoke(command, [&](CommandRunner::Result &&result) {
        return_code = result.return_code;
      });
      while (!runner.empty()) {
        runner.runCommands();
      }

      CHECK(return_code != 0);
    }
  }

  SECTION("checkCommand") {
    SECTION("empty command") {
      const auto empty_command = DummyCommandRunner::constructCommand({}, {});
      DummyCommandRunner::checkCommand(file_system, empty_command);
    }

    rc::prop("checkCommand after runCommand", []() {
      const auto paths = std::make_shared<Paths>();
      InMemoryFileSystem file_system(*paths);
      DummyCommandRunner runner(*paths, file_system);
      const auto inputs = *gen::pathWithSingleComponentVector(paths);
      const auto outputs = *rc::gen::nonEmpty(
          gen::pathWithSingleComponentVector(paths));

      const auto command = DummyCommandRunner::constructCommand(inputs, outputs);

      // The command is not run yet so should not pass
      CHECK_THROWS(DummyCommandRunner::checkCommand(file_system, command));

      runner.invoke(command, CommandRunner::noopCallback);
      while (!runner.empty()) {
        runner.runCommands();
      }

      DummyCommandRunner::checkCommand(file_system, command);
    });
  }
}

}  // namespace shk
