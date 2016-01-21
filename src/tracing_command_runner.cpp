#include "tracing_command_runner.h"

#include <assert.h>

#include "sandbox_parser.h"

namespace shk {
namespace {

Path generateTemporaryPath(Paths &paths) throw(IoError) {
  // TODO(peck): Implement me
  return paths.get("a");
}

class TemporaryFile {
 public:
  TemporaryFile(FileSystem &file_system) throw(IoError)
      : path(generateTemporaryPath(file_system.paths())),
        _file_system(file_system) {}

  ~TemporaryFile() {
    try {
      _file_system.unlink(path);
    } catch (IoError &) {
      // Maybe the file is already gone, or was never created. We don't care
      // enough to make sure to clean up this temporary file.
    }
  }

  const Path path;

 private:
  FileSystem &_file_system;
};

class TracingCommandRunner : public CommandRunner {
 public:
  TracingCommandRunner(
      FileSystem &file_system,
      std::unique_ptr<CommandRunner> &&inner)
      : _file_system(file_system),
        _inner(std::move(inner)) {}

  void invoke(
      const std::string &command,
      UseConsole use_console,
      const Callback &callback) override {
    // TODO(peck): Handle tmp file creation error
    const auto tmp = std::make_shared<TemporaryFile>(_file_system);

    // Here we assume that the generated temporary file path does not contain
    // ' or ". It would be an evil temporary file creation function that would
    // do that.
    _inner->invoke(
        "/usr/bin/sandbox-exec -p '(version 1)(trace \"" +
            tmp->path.canonicalized() + "\")' " + command,
        use_console,
        [this, tmp, callback](CommandRunner::Result &&result) {
          computeResults(tmp->path, result);
          callback(std::move(result));
        });
  }

  size_t size() const override {
    return _inner->size();
  }

  bool runCommands() override {
    return _inner->runCommands();
  }

 private:
  void computeResults(
      Path path,
      CommandRunner::Result &result) {
    if (result.exit_status != ExitStatus::SUCCESS) {
      // If the command did not suceed there is no need to track dependencies.
      // Trying to do so might not even work, which could cause confusing
      // extraenous error messages.
      return;
    }
    try {
      auto sandbox =
          parseSandbox(_file_system.paths(), _file_system.readFile(path));

      std::copy(
          sandbox.read.begin(),
          sandbox.read.end(),
          std::back_inserter(result.input_files));

      std::copy(
          sandbox.created.begin(),
          sandbox.created.end(),
          std::back_inserter(result.output_files));

      assert(result.linting_errors.empty());
      result.linting_errors = std::move(sandbox.violations);
    } catch (ParseError &error) {
      result.linting_errors.emplace_back(
          std::string("Failed to parse sandbox file: ") + error.what());
    } catch (IoError &error) {
      result.linting_errors.emplace_back(
          std::string("Failed to open sandbox file: ") + error.what());
    }
  }

  FileSystem &_file_system;
  const std::unique_ptr<CommandRunner> _inner;
};

}

std::unique_ptr<CommandRunner> makeTracingCommandRunner(
    FileSystem &file_system,
    std::unique_ptr<CommandRunner> &&command_runner) {
  return std::unique_ptr<CommandRunner>(
      new TracingCommandRunner(file_system, std::move(command_runner)));
}

}  // namespace shk
