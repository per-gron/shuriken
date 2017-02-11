#include <catch.hpp>

#include "cmd/real_command_runner.h"
#include "cmd/tracing_command_runner.h"
#include "fs/persistent_file_system.h"
#include "util.h"

namespace shk {
namespace {

CommandRunner::Result runCommand(
    CommandRunner &runner,
    const std::string &command) {
  CommandRunner::Result result;

  bool did_finish = false;
  runner.invoke(
      command,
      "a_pool",
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

class FailingMkstempFileSystem : public FileSystem {
 public:
  FailingMkstempFileSystem()
      : _fs(persistentFileSystem()) {}

  std::unique_ptr<Stream> open(
      const std::string &path, const char *mode) throw(IoError) override {
    return _fs->open(path, mode);
  }
  std::unique_ptr<Mmap> mmap(
      const std::string &path) throw(IoError) override {
    return _fs->mmap(path);
  }
  Stat stat(const std::string &path) override {
    return _fs->stat(path);
  }
  Stat lstat(const std::string &path) override {
    return _fs->lstat(path);
  }
  void mkdir(const std::string &path) throw(IoError) override {
    _fs->mkdir(path);
  }
  void rmdir(const std::string &path) throw(IoError) override {
    _fs->rmdir(path);
  }
  void unlink(const std::string &path) throw(IoError) override {
    _fs->unlink(path);
  }
  void rename(
      const std::string &old_path,
      const std::string &new_path) throw(IoError) override {
    _fs->rename(old_path, new_path);
  }
  void truncate(
      const std::string &path, size_t size) throw(IoError) override {
    _fs->truncate(path, size);
  }
  std::vector<DirEntry> readDir(
      const std::string &path) throw(IoError) override {
    return _fs->readDir(path);
  }
  std::string readFile(const std::string &path) throw(IoError) override {
    return _fs->readFile(path);
  }
  Hash hashFile(const std::string &path) throw(IoError) override {
    return _fs->hashFile(path);
  }
  std::string mkstemp(std::string &&filename_template) throw(IoError) override {
    throw IoError("Test-induced mkstemp error", 0);
  }

 private:
  std::unique_ptr<FileSystem> _fs;
};

class FailingUnlinkFileSystem : public FileSystem {
 public:
  FailingUnlinkFileSystem()
      : _fs(persistentFileSystem()) {}

  std::unique_ptr<Stream> open(
      const std::string &path, const char *mode) throw(IoError) override {
    return _fs->open(path, mode);
  }
  std::unique_ptr<Mmap> mmap(
      const std::string &path) throw(IoError) override {
    return _fs->mmap(path);
  }
  Stat stat(const std::string &path) override {
    return _fs->stat(path);
  }
  Stat lstat(const std::string &path) override {
    return _fs->lstat(path);
  }
  void mkdir(const std::string &path) throw(IoError) override {
    _fs->mkdir(path);
  }
  void rmdir(const std::string &path) throw(IoError) override {
    _fs->rmdir(path);
  }
  void unlink(const std::string &path) throw(IoError) override {
    // Unlink it anyway, because we don't want to leave files around on the
    // file system after the test has finished running.
    _fs->unlink(path);
    throw IoError("Test-induced unlink error", 0);
  }
  void rename(
      const std::string &old_path,
      const std::string &new_path) throw(IoError) override {
    _fs->rename(old_path, new_path);
  }
  void truncate(
      const std::string &path, size_t size) throw(IoError) override {
    _fs->truncate(path, size);
  }
  std::vector<DirEntry> readDir(
      const std::string &path) throw(IoError) override {
    return _fs->readDir(path);
  }
  std::string readFile(const std::string &path) throw(IoError) override {
    return _fs->readFile(path);
  }
  Hash hashFile(const std::string &path) throw(IoError) override {
    return _fs->hashFile(path);
  }
  std::string mkstemp(std::string &&filename_template) throw(IoError) override {
    return _fs->mkstemp(std::move(filename_template));
  }

 private:
  std::unique_ptr<FileSystem> _fs;
};

template<typename Container, typename Value>
bool contains(const Container &container, const Value &value) {
  return container.find(value) != container.end();
}

}  // anonymous namespace

TEST_CASE("TracingCommandRunner") {
  const auto fs = persistentFileSystem();
  const auto runner = makeTracingCommandRunner(
      *fs,
      makeRealCommandRunner());
  const auto output_path = getWorkingDir() + "/shk.test-file";

  SECTION("TrackInputs") {
    const auto result = runCommand(*runner, "");
    CHECK(result.exit_status == ExitStatus::SUCCESS);
    CHECK(result.input_files.empty());
    CHECK(result.output_files.empty());
  }

  SECTION("TrackInputs") {
    const auto result = runCommand(*runner, "/bin/ls /sbin");
    CHECK(contains(result.input_files, "/sbin"));
    CHECK(contains(result.input_files, "/bin/ls"));
    CHECK(result.output_files.empty());
  }

  SECTION("IgnoreReadOfWorkingDir") {
    std::string escaped_working_dir;
    getShellEscapedString(getWorkingDir(), &escaped_working_dir);
    const auto result = runCommand(*runner, "/bin/ls " + escaped_working_dir);
    CHECK(!contains(result.input_files, getWorkingDir()));
  }

  SECTION("TrackOutputs") {
    const auto result = runCommand(
        *runner, "/usr/bin/touch " + output_path);
    CHECK(result.output_files.size() == 1);
    CHECK(contains(result.output_files, output_path));
    fs->unlink(output_path);
  }

  SECTION("TrackRemovedOutputs") {
    const auto result = runCommand(
        *runner,
        "/usr/bin/touch '" + output_path + "'; /bin/rm '" +
        output_path + "'");
    CHECK(result.output_files.empty());
  }

  SECTION("TrackMovedOutputs") {
    const auto other_path = output_path + ".b";
    const auto result = runCommand(
        *runner,
        "/usr/bin/touch " + output_path + " && /bin/mv " +
        output_path + " " + other_path);
    CHECK(result.output_files.size() == 1);
    // Should have only other_path as an output path; the file at output_path
    // was moved.
    CHECK(contains(result.output_files, other_path));
    fs->unlink(other_path);
  }

  SECTION("HandleTmpFileCreationError") {
    FailingMkstempFileSystem failing_mkstemp;
    const auto runner = makeTracingCommandRunner(
        failing_mkstemp,
        makeRealCommandRunner());

    // Failing to create tmpfile should not make invoke throw
    const auto result = runCommand(*runner, "/bin/echo");

    // But it should make the command fail
    CHECK(result.exit_status == ExitStatus::FAILURE);
  }

  SECTION("HandleTmpFileRemovalError") {
    FailingUnlinkFileSystem failing_unlink;
    const auto runner = makeTracingCommandRunner(
        failing_unlink,
        makeRealCommandRunner());

    const auto result = runCommand(*runner, "/bin/ls /sbin");
    CHECK(contains(result.input_files, "/bin/ls"));

    // Failing to remove the tempfile should be ignored
  }

  SECTION("abort") {
    CommandRunner::Result result;

    runner->invoke(
        "/bin/echo",
        "a_pool",
        CommandRunner::noopCallback);
  }

  SECTION("size") {
    CommandRunner::Result result;

    runner->invoke(
        "/bin/echo",
        "a_pool",
        CommandRunner::noopCallback);

    CHECK(runner->size() == 1);

    runner->invoke(
        "/bin/echo",
        "a_pool",
        CommandRunner::noopCallback);

    CHECK(runner->size() == 2);

    while (!runner->empty()) {
      // Pretend we discovered that stderr was ready for writing.
      runner->runCommands();
    }
  }
}

}  // namespace shk
