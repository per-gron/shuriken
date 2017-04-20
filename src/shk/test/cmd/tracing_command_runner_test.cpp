#include <catch.hpp>

#include <util/shktrace.h>

#include "../in_memory_file_system.h"
#include "../manifest/step_builder.h"
#include "cmd/tracing_command_runner.h"
#include "util.h"

namespace shk {
namespace {

class MockTraceServerHandle : public TraceServerHandle {
 public:
  virtual const std::string &getShkTracePath() override {
    return _executable_path;
  }

  virtual bool startServer(std::string *err) override {
    if (_start_error.empty()) {
      return true;
    } else {
      *err = _start_error;
      return false;
    }
  }

  void setStartError(const std::string &err) {
    _start_error = err;
  }

 private:
  std::string _executable_path = "exec_path";
  std::string _start_error;
};

CommandRunner::Result runCommand(
    CommandRunner &runner,
    const std::string &command) {
  CommandRunner::Result result;

  flatbuffers::FlatBufferBuilder builder;

  bool did_finish = false;
  runner.invoke(
      command,
      StepBuilder().setPoolName("a_pool").build(builder),
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
  std::unique_ptr<Stream> open(
      nt_string_view path, const char *mode) throw(IoError) override {
    return _fs.open(path, mode);
  }
  std::unique_ptr<Mmap> mmap(nt_string_view path) throw(IoError) override {
    return _fs.mmap(path);
  }
  Stat stat(nt_string_view path) override {
    return _fs.stat(path);
  }
  Stat lstat(nt_string_view path) override {
    return _fs.lstat(path);
  }
  void mkdir(nt_string_view path) throw(IoError) override {
    _fs.mkdir(path);
  }
  void rmdir(nt_string_view path) throw(IoError) override {
    _fs.rmdir(path);
  }
  void unlink(nt_string_view path) throw(IoError) override {
    _fs.unlink(path);
  }
  void symlink(
      nt_string_view target,
      nt_string_view source) throw(IoError) override {
    _fs.symlink(target, source);
  }
  void rename(
      nt_string_view old_path,
      nt_string_view new_path) throw(IoError) override {
    _fs.rename(old_path, new_path);
  }
  void truncate(
      nt_string_view path, size_t size) throw(IoError) override {
    _fs.truncate(path, size);
  }
  std::vector<DirEntry> readDir(
      nt_string_view path) throw(IoError) override {
    return _fs.readDir(path);
  }
  std::string readSymlink(nt_string_view path) throw(IoError) override {
    return _fs.readSymlink(path);
  }
  std::string readFile(nt_string_view path) throw(IoError) override {
    return _fs.readFile(path);
  }
  Hash hashFile(nt_string_view path) throw(IoError) override {
    return _fs.hashFile(path);
  }
  std::string mkstemp(std::string &&filename_template) throw(IoError) override {
    throw IoError("Test-induced mkstemp error", 0);
  }

 private:
  InMemoryFileSystem _fs;
};

class FailingUnlinkFileSystem : public FileSystem {
 public:
  std::unique_ptr<Stream> open(
      nt_string_view path, const char *mode) throw(IoError) override {
    return _fs.open(path, mode);
  }
  std::unique_ptr<Mmap> mmap(nt_string_view path) throw(IoError) override {
    return _fs.mmap(path);
  }
  Stat stat(nt_string_view path) override {
    return _fs.stat(path);
  }
  Stat lstat(nt_string_view path) override {
    return _fs.lstat(path);
  }
  void mkdir(nt_string_view path) throw(IoError) override {
    _fs.mkdir(path);
  }
  void rmdir(nt_string_view path) throw(IoError) override {
    _fs.rmdir(path);
  }
  void unlink(nt_string_view path) throw(IoError) override {
    throw IoError("Test-induced unlink error", 0);
  }
  void symlink(
      nt_string_view target,
      nt_string_view source) throw(IoError) override {
    _fs.symlink(target, source);
  }
  void rename(
      nt_string_view old_path,
      nt_string_view new_path) throw(IoError) override {
    _fs.rename(old_path, new_path);
  }
  void truncate(
      nt_string_view path, size_t size) throw(IoError) override {
    _fs.truncate(path, size);
  }
  std::vector<DirEntry> readDir(
      nt_string_view path) throw(IoError) override {
    return _fs.readDir(path);
  }
  std::string readSymlink(nt_string_view path) throw(IoError) override {
    return _fs.readSymlink(path);
  }
  std::string readFile(nt_string_view path) throw(IoError) override {
    return _fs.readFile(path);
  }
  Hash hashFile(nt_string_view path) throw(IoError) override {
    return _fs.hashFile(path);
  }
  std::string mkstemp(std::string &&filename_template) throw(IoError) override {
    return _fs.mkstemp(std::move(filename_template));
  }

 private:
  InMemoryFileSystem _fs;
};

template<typename Container, typename Value>
bool contains(const Container &container, const Value &value) {
  return std::find(
      container.begin(), container.end(), value) != container.end();
}

class MockCommandRunner : public CommandRunner {
 public:
  struct Command {
    std::string command;
    Step step;
    Callback callback;
  };

  virtual ~MockCommandRunner() {
    CHECK(_inspected_command_idx == _commands.size());
  }

  virtual void invoke(
      nt_string_view command,
      Step step,
      const Callback &callback) override {
    Command cmd{
        std::string(command),
        step,
        callback };
    _commands.push_back(std::move(cmd));
  }

  virtual size_t size() const override {
    return _commands.size() - _ran_command_idx;
  }

  virtual bool canRunMore() const override {
    return _can_run_more;
  }

  void setCanRunMore(bool can_run_more) {
    _can_run_more = can_run_more;
  }

  virtual bool runCommands() override {
    for (; _ran_command_idx < _commands.size(); _ran_command_idx++) {
      const auto &cmd = _commands[_ran_command_idx];
      Result result;
      if (cmd.command ==
              "/bin/echo Failed to create temporary file && exit 1") {
        result.exit_status = ExitStatus::FAILURE;
      }
      cmd.callback(std::move(result));
    }
    return false;
  }

  const std::vector<Command> outstandingCommands() const {
    return _commands;
  }

  Command popCommand() {
    REQUIRE(_inspected_command_idx < _commands.size());
    return _commands[_inspected_command_idx++];
  }

 private:
  int _inspected_command_idx = 0;
  int _ran_command_idx = 0;
  std::vector<Command> _commands;
  bool _can_run_more = true;
};

std::string makeTrace(
    const std::vector<std::string> &inputs,
    const std::vector<std::string> &outputs,
    const std::vector<std::string> &errors) {
  flatbuffers::FlatBufferBuilder builder(1024);

  // inputs
  std::vector<flatbuffers::Offset<flatbuffers::String>> input_offsets;
  input_offsets.reserve(inputs.size());

  for (const auto &input : inputs) {
    input_offsets.push_back(builder.CreateString(input));
  }
  auto input_vector = builder.CreateVector(
      input_offsets.data(), input_offsets.size());

  // outputs
  std::vector<flatbuffers::Offset<flatbuffers::String>> output_offsets;
  output_offsets.reserve(outputs.size());

  for (const auto &output : outputs) {
    output_offsets.push_back(builder.CreateString(output));
  }
  auto output_vector = builder.CreateVector(
      output_offsets.data(), output_offsets.size());

  // errors
  std::vector<flatbuffers::Offset<flatbuffers::String>> error_offsets;
  error_offsets.reserve(errors.size());

  for (const auto &error : errors) {
    error_offsets.push_back(builder.CreateString(error));
  }

  auto error_vector = builder.CreateVector(
      error_offsets.data(), error_offsets.size());

  builder.Finish(CreateTrace(builder, input_vector, output_vector, error_vector));

  return std::string(
      reinterpret_cast<const char *>(builder.GetBufferPointer()),
      builder.GetSize());
}


}  // anonymous namespace

TEST_CASE("TracingCommandRunner") {
  auto mock_trace_server_handle_ptr = std::unique_ptr<MockTraceServerHandle>(
      new MockTraceServerHandle());
  auto &mock_trace_server_handle = *mock_trace_server_handle_ptr.get();
  auto mock_command_runner_ptr = std::unique_ptr<MockCommandRunner>(
      new MockCommandRunner());
  auto &mock_command_runner = *mock_command_runner_ptr.get();
  InMemoryFileSystem fs;
  const auto runner = makeTracingCommandRunner(
      std::move(mock_trace_server_handle_ptr),
      fs,
      std::move(mock_command_runner_ptr));

  SECTION("EmptyCommand") {
    const auto result = runCommand(*runner, "");
    CHECK(result.exit_status == ExitStatus::SUCCESS);
    CHECK(result.input_files.empty());
    CHECK(result.output_files.empty());

    const auto cmd = mock_command_runner.popCommand();
    CHECK(cmd.command == "");
  }

  SECTION("StartError") {
    mock_trace_server_handle.setStartError("hey");
    CHECK_THROWS_AS(runCommand(*runner, "cmd"), IoError);
  }

  SECTION("HandleTmpFileCreationError") {
    auto mock_command_runner_ptr = std::unique_ptr<MockCommandRunner>(
        new MockCommandRunner());
    auto &mock_command_runner = *mock_command_runner_ptr.get();

    FailingMkstempFileSystem failing_mkstemp;
    const auto runner = makeTracingCommandRunner(
        std::unique_ptr<TraceServerHandle>(new MockTraceServerHandle()),
        failing_mkstemp,
        std::move(mock_command_runner_ptr));

    // Failing to create tmpfile should not make invoke throw
    const auto result = runCommand(*runner, "/bin/echo");

    // But it should make the command fail
    CHECK(result.exit_status == ExitStatus::FAILURE);

    mock_command_runner.popCommand();
  }

  SECTION("EscapeCommand") {
    const auto result = runCommand(*runner, "h'ey");

    const auto cmd = mock_command_runner.popCommand();
    CHECK(cmd.command.rfind("-c 'h'\\''ey'") != std::string::npos);
  }

  SECTION("InvokeShkTraceWithProperArgs") {
    fs.enqueueMkstempResult("temp_file");
    const auto result = runCommand(*runner, "cmd");

    const auto cmd = mock_command_runner.popCommand();
    CHECK(cmd.command == "exec_path -f 'temp_file' -c cmd");
  }

  SECTION("NoTrace") {
    fs.enqueueMkstempResult("trace");

    const auto result = runCommand(*runner, "hey there");
    mock_command_runner.popCommand();

    CHECK(result.exit_status == ExitStatus::FAILURE);
    CHECK(result.output ==
        "shk: Failed to open trace file: No such file or directory\n");
  }

  SECTION("InvalidTrace") {
    fs.enqueueMkstempResult("trace");
    fs.writeFile("trace", "hej");

    const auto result = runCommand(*runner, "hey there");
    mock_command_runner.popCommand();

    CHECK(result.exit_status == ExitStatus::FAILURE);
    CHECK(result.output ==
        "shk: Trace file did not pass validation\n");
  }

  SECTION("TrackInputsAndOutputs") {
    const auto trace = makeTrace(
        { { "in1" }, { "in2" } },
        { "out" },
        {});
    fs.enqueueMkstempResult("trace");
    fs.writeFile("trace", trace);

    const auto result = runCommand(*runner, "hey there");
    mock_command_runner.popCommand();

    CHECK(result.exit_status == ExitStatus::SUCCESS);
    CHECK(contains(result.input_files, "in1"));
    CHECK(contains(result.input_files, "in2"));
    CHECK(contains(result.output_files, "out"));
    CHECK(result.output.empty());
  }

  SECTION("HandleTmpFileRemovalError") {
    FailingUnlinkFileSystem failing_unlink;

    auto mock_command_runner_ptr = std::unique_ptr<MockCommandRunner>(
        new MockCommandRunner());
    auto &mock_command_runner = *mock_command_runner_ptr.get();

    FailingMkstempFileSystem failing_mkstemp;
    const auto runner = makeTracingCommandRunner(
        std::unique_ptr<TraceServerHandle>(new MockTraceServerHandle()),
        failing_unlink,
        std::move(mock_command_runner_ptr));

    fs.enqueueMkstempResult("trace");
    fs.writeFile("trace", makeTrace({}, {}, {}));

    const auto result = runCommand(*runner, "lolol");
    mock_command_runner.popCommand();

    // Failing to remove the tempfile should be ignored
  }

  SECTION("Size") {
    flatbuffers::FlatBufferBuilder builder;
    auto step = StepBuilder().setPoolName("b").build(builder);

    CHECK(runner->size() == 0);
    mock_command_runner.invoke("a", step, [](
        CommandRunner::Result &&result) {});
    CHECK(runner->size() == 1);
    mock_command_runner.popCommand();
  }

  SECTION("CanRunMore") {
    CHECK(runner->canRunMore());
    mock_command_runner.setCanRunMore(false);
    CHECK(!runner->canRunMore());
  }

  SECTION("ParseTrace") {
    SECTION("InitialFailure") {
      const auto trace = makeTrace(
          {}, {}, {});
      CommandRunner::Result result;
      result.exit_status = ExitStatus::FAILURE;
      detail::parseTrace(trace, &result);

      CHECK(result.exit_status == ExitStatus::FAILURE);
    }

    SECTION("Inputs") {
      const auto trace = makeTrace(
          { { "hi" }, { "dir" } }, {}, {});
      CommandRunner::Result result;
      detail::parseTrace(trace, &result);

      CHECK(result.exit_status == ExitStatus::SUCCESS);
      CHECK(result.input_files.size() == 2);
      CHECK(contains(result.input_files, "hi"));
      CHECK(contains(result.input_files, "dir"));
      CHECK(result.output_files.empty());
      CHECK(result.output.empty());
    }

    SECTION("Outputs") {
      const auto trace = makeTrace(
          {}, { "out" }, {});
      CommandRunner::Result result;
      detail::parseTrace(trace, &result);

      CHECK(result.exit_status == ExitStatus::SUCCESS);
      CHECK(result.input_files.empty());
      CHECK(result.output_files.size() == 1);
      CHECK(contains(result.output_files, "out"));
      CHECK(result.output.empty());
    }

    SECTION("Errors") {
      const auto trace = makeTrace(
          {}, {}, { "err"});
      CommandRunner::Result result;
      detail::parseTrace(trace, &result);

      CHECK(result.exit_status == ExitStatus::FAILURE);
      CHECK(result.input_files.empty());
      CHECK(result.output_files.empty());
      CHECK(result.output == "shk: err\n");
    }

    SECTION("IgnoredPaths") {
      const auto trace = makeTrace(
          { { "/dev/null" }, { "/AppleInternal" } },
          { "/dev/urandom" },
          {});
      CommandRunner::Result result;
      detail::parseTrace(trace, &result);

      CHECK(result.exit_status == ExitStatus::SUCCESS);
      CHECK(result.input_files.empty());
      CHECK(result.output_files.empty());
      CHECK(result.output.empty());
    }
  }
}

}  // namespace shk
