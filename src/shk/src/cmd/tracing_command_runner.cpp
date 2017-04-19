#include "cmd/tracing_command_runner.h"

#include <assert.h>
#include <unordered_set>

#include <util/shktrace.h>

#include "util.h"

namespace shk {
namespace {

std::unordered_set<std::string> makeIgnoredFilesSet() {
  static const char * const kIgnoredFilesList[] = {
    "/AppleInternal",
    "/dev/null",
    "/dev/random",
    "/dev/urandom",
    "/dev/dtracehelper",
    "/dev/tty" };

  std::unordered_set<std::string> result;
  for (const auto *file : kIgnoredFilesList) {
    result.insert(file);
  }
  return result;
}

const std::unordered_set<std::string> kIgnoredFiles = makeIgnoredFilesSet();

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
      const std::shared_ptr<TraceServerHandle> &trace_sever_handle,
      FileSystem &file_system,
      std::unique_ptr<CommandRunner> &&inner)
      : _trace_server_handle(trace_sever_handle),
        _escaped_shk_trace_cmd(
            shellEscape(_trace_server_handle->getShkTracePath())),
        _file_system(file_system),
        _inner(std::move(inner)) {}

  void invoke(
      nt_string_view command,
      nt_string_view pool_name,
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

  const std::shared_ptr<TraceServerHandle> _trace_server_handle;
  const std::string _escaped_shk_trace_cmd;
  FileSystem &_file_system;
  const std::unique_ptr<CommandRunner> _inner;
};

}

namespace detail {

void parseTrace(string_view trace_view, CommandRunner::Result *result) {
  flatbuffers::Verifier verifier(
      reinterpret_cast<const uint8_t *>(trace_view.data()),
      trace_view.size());
  if (!VerifyTraceBuffer(verifier)) {
    result->output += "shk: Trace file did not pass validation\n";
    result->exit_status = ExitStatus::FAILURE;
    return;
  }

  auto trace = GetTrace(trace_view.data());

  for (int i = 0; i < trace->inputs()->size(); i++) {
    const auto *input = trace->inputs()->Get(i);
    if (kIgnoredFiles.find(input->c_str()) == kIgnoredFiles.end()) {
      result->input_files.push_back(input->c_str());
    }
  }

  for (int i = 0; i < trace->outputs()->size(); i++) {
    const auto *output = trace->outputs()->Get(i);
    if (kIgnoredFiles.find(output->c_str()) == kIgnoredFiles.end()) {
      result->output_files.push_back(output->c_str());
    }
  }

  for (int i = 0; i < trace->errors()->size(); i++) {
    result->output +=
        "shk: " + std::string(trace->errors()->Get(i)->c_str()) + "\n";
    result->exit_status = ExitStatus::FAILURE;
  }
}

}  // namespace detail

std::unique_ptr<CommandRunner> makeTracingCommandRunner(
    const std::shared_ptr<TraceServerHandle> &trace_sever_handle,
    FileSystem &file_system,
    std::unique_ptr<CommandRunner> &&command_runner) {
  return std::unique_ptr<CommandRunner>(
      new TracingCommandRunner(
          std::move(trace_sever_handle),
          file_system,
          std::move(command_runner)));
}

}  // namespace shk
