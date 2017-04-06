#include "cmd/tracing_command_runner.h"

#include <assert.h>

#include <util/shktrace.h>

#include "cmd/sandbox_parser.h"
#include "util.h"

namespace shk {
namespace {

class TemporaryFile {
 public:
  TemporaryFile(FileSystem &file_system) throw(IoError)
      : path(file_system.mkstemp("shk.tmp.sb.XXXXXXXX")),
        _file_system(file_system) {}

  ~TemporaryFile() {
    try {
      _file_system.unlink(path);
    } catch (const IoError &) {
      // Maybe the file is already gone, or was never created. We don't care
      // enough to make sure to clean up this temporary file.
    }
  }

  const std::string path;

 private:
  FileSystem &_file_system;
};

std::string shellEscape(const std::string &cmd) {
  std::string escaped_cmd;
  getShellEscapedString(cmd, &escaped_cmd);
  return escaped_cmd;
}

class TracingCommandRunner : public CommandRunner {
 public:
  TracingCommandRunner(
      std::unique_ptr<TraceServerHandle> &&trace_sever_handle,
      FileSystem &file_system,
      std::unique_ptr<CommandRunner> &&inner)
      : _trace_server_handle(std::move(trace_sever_handle)),
        _escaped_shk_trace_cmd(
            shellEscape(_trace_server_handle->getShkTracePath())),
        _file_system(file_system),
        _inner(std::move(inner)) {}

  void invoke(
      const std::string &command,
      const std::string &pool_name,
      const Callback &callback) override {
    if (command.empty()) {
      _inner->invoke("", pool_name, callback);
      return;
    }

    std::string err;
    if (!_trace_server_handle->startServer(&err)) {
      throw IoError("Failed to start shk-trace server: " + err, 0);
    }

    try {
      const auto tmp = std::make_shared<TemporaryFile>(_file_system);

      std::string escaped_command;
      getShellEscapedString(command, &escaped_command);
      // Here we assume that the generated temporary file path does not contain
      // ' or ". It would be an evil temporary file creation function that would
      // do that.
      _inner->invoke(
          _escaped_shk_trace_cmd + ""
              " -f '" + tmp->path + "'"
              " -c " + escaped_command,
          pool_name,
          [this, tmp, callback](CommandRunner::Result &&result) {
            computeResults(tmp->path, result);
            callback(std::move(result));
          });
    } catch (const IoError &error) {
      _inner->invoke(
          "/bin/echo Failed to create temporary file && exit 1",
          pool_name,
          callback);
    }
  }

  size_t size() const override {
    return _inner->size();
  }

  bool canRunMore() const override {
    return _inner->canRunMore();
  }

  bool runCommands() override {
    return _inner->runCommands();
  }

 private:
  void computeResults(
      const std::string &path,
      CommandRunner::Result &result) {
    if (result.exit_status != ExitStatus::SUCCESS) {
      // If the command did not suceed there is no need to track dependencies.
      // Trying to do so might not even work, which could cause confusing
      // extraenous error messages.
      return;
    }
    try {
      auto mmap = _file_system.mmap(path);
      detail::parseTrace(mmap->memory(), &result);
    } catch (const IoError &error) {
      result.output +=
          std::string("shk: Failed to open trace file: ") + error.what() + "\n";
      result.exit_status = ExitStatus::FAILURE;
    }
  }

  const std::unique_ptr<TraceServerHandle> _trace_server_handle;
  const std::string _escaped_shk_trace_cmd;
  FileSystem &_file_system;
  const std::unique_ptr<CommandRunner> _inner;
};

}

namespace detail {

void parseTrace(StringPiece trace_slice, CommandRunner::Result *result) {
  flatbuffers::Verifier verifier(
      reinterpret_cast<const uint8_t *>(trace_slice._str),
      trace_slice._len);
  if (!VerifyTraceBuffer(verifier)) {
    result->output += "shk: Trace file did not pass validation\n";
    result->exit_status = ExitStatus::FAILURE;
    return;
  }

  auto trace = GetTrace(trace_slice._str);

  for (int i = 0; i < trace->inputs()->size(); i++) {
    const auto *input = trace->inputs()->Get(i);
    result->input_files.emplace(
        input->path()->c_str(),
        input->directory_listing() ?
            DependencyType::ALWAYS :
            DependencyType::IGNORE_IF_DIRECTORY);
  }

  for (int i = 0; i < trace->outputs()->size(); i++) {
    result->output_files.insert(trace->outputs()->Get(i)->c_str());
  }

  for (int i = 0; i < trace->errors()->size(); i++) {
    result->output +=
        "shk: " + std::string(trace->errors()->Get(i)->c_str()) + "\n";
    result->exit_status = ExitStatus::FAILURE;
  }

  // Linking steps tend to read the contents of the working directory for
  // some reason, which causes them to always be treated as dirty, which
  // obviously is not good. This is a hack to work around that, but it also
  // means that build steps can't depend on the contents of the build
  // directory.
  result->input_files.erase(getWorkingDir());

  static const char * const kIgnoredFiles[] = {
      "/AppleInternal",
      "/dev/null",
      "/dev/random",
      "/dev/urandom",
      "/dev/dtracehelper",
      "/dev/tty" };

  for (const auto *ignored_file : kIgnoredFiles) {
    result->input_files.erase(ignored_file);
    result->output_files.erase(ignored_file);
  }
}

}  // namespace detail

std::unique_ptr<CommandRunner> makeTracingCommandRunner(
    std::unique_ptr<TraceServerHandle> &&trace_sever_handle,
    FileSystem &file_system,
    std::unique_ptr<CommandRunner> &&command_runner) {
  return std::unique_ptr<CommandRunner>(
      new TracingCommandRunner(
          std::move(trace_sever_handle),
          file_system,
          std::move(command_runner)));
}

}  // namespace shk
