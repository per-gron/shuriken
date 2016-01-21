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

std::string getWorkingDir() {
  char *wd = getcwd(NULL, 0);
  std::string result = wd;
  free(wd);
  return result;
}

}  // anonymous namespace

TEST_CASE("TracingCommandRunner") {
  Paths paths;
  const auto fs = persistentFileSystem(paths);
  const auto runner = makeTracingCommandRunner(
      *fs,
      makeRealCommandRunner());
  const auto output_path = paths.get(getWorkingDir() + "/shk.test-file");

  SECTION("TrackInputs") {
    const auto result = runCommand(*runner, "/bin/ls /sbin");
    CHECK(contains(result.input_files, paths.get("/sbin")));
    CHECK(contains(result.input_files, paths.get("/bin/ls")));
    CHECK(result.output_files.empty());
  }

  SECTION("TrackOutputs") {
    const auto result = runCommand(
        *runner, "/usr/bin/touch " + output_path.canonicalized());
    CHECK(result.output_files.size() == 1);
    CHECK(contains(result.output_files, output_path));
    fs->unlink(output_path);
  }

  SECTION("TrackRemovedOutputs") {
    const auto result = runCommand(
        *runner,
        "/usr/bin/touch '" + output_path.canonicalized() + "'; /bin/rm '" +
        output_path.canonicalized() + "'");
    CHECK(result.output_files.empty());
  }

  SECTION("TrackMovedOutputs") {
    const auto other_path = paths.get(output_path.canonicalized() + ".b");
    const auto result = runCommand(
        *runner,
        "/usr/bin/touch " + output_path.canonicalized() + " && /bin/mv " +
        output_path.canonicalized() + " " + other_path.canonicalized());
    CHECK(result.output_files.size() == 1);
    // Should have only other_path as an output path; the file at output_path
    // was moved.
    CHECK(contains(result.output_files, other_path));
    fs->unlink(other_path);
  }

  SECTION("HandleTmpFileCreationError") {
    // FIXME(peck)
  }

  SECTION("HandleTmpFileRemovalError") {
    // FIXME(peck)
  }

  SECTION("abort") {
    CommandRunner::Result result;

    runner->invoke(
        "/bin/echo",
        UseConsole::NO,
        CommandRunner::noopCallback);
  }

  SECTION("size") {
    CommandRunner::Result result;

    runner->invoke(
        "/bin/echo",
        UseConsole::NO,
        CommandRunner::noopCallback);

    CHECK(runner->size() == 1);

    runner->invoke(
        "/bin/echo",
        UseConsole::NO,
        CommandRunner::noopCallback);

    CHECK(runner->size() == 2);

    while (!runner->empty()) {
      // Pretend we discovered that stderr was ready for writing.
      runner->runCommands();
    }
  }
}

}  // namespace shk
