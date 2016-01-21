#include <catch.hpp>

#include "persistent_file_system.h"
#include "real_command_runner.h"
#include "tracing_command_runner.h"

namespace shk {
namespace {

CommandRunner::Result runCommand(
    CommandRunner &runner,
    const std::string &command) {
  CommandRunner::Result result;

  bool did_finish = false;
  runner.invoke(
      command,
      UseConsole::NO,
      [&](CommandRunner::Result &&result_) {
        result = std::move(result_);
        did_finish = true;
      });

  while (!runner.empty()) {
    // Pretend we discovered that stderr was ready for writing.
    runner.runCommands();
  }

  CHECK(did_finish);

  return result;
}

template<typename Container, typename Value>
bool contains(const Container &container, const Value &value) {
  return std::find(
      container.begin(), container.end(), value) != container.end();
}

}  // anonymous namespace

TEST_CASE("TracingCommandRunner") {
  Paths paths;
  const auto fs = persistentFileSystem(paths);
  const auto runner = makeTracingCommandRunner(
      *fs,
      makeRealCommandRunner());

  const auto result = runCommand(*runner, "/bin/ls /sbin");
  CHECK(contains(result.input_files, paths.get("/sbin")));
  CHECK(contains(result.input_files, paths.get("/bin/ls")));
  CHECK(result.output_files.empty());
}

}  // namespace shk
