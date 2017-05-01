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
    "/dev/autofs_nowait",
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
  TemporaryFile(const TemporaryFile &) = delete;
  TemporaryFile &operator=(const TemporaryFile &) = delete;

  static std::pair<std::shared_ptr<TemporaryFile>, bool> make(
      FileSystem &file_system, std::string *err) {
    std::string path;
    IoError error;
    std::tie(path, error) = file_system.mkstemp("shk.tmp.sb.XXXXXXXX");
    if (error) {
      return std::pair<std::shared_ptr<TemporaryFile>, bool>(nullptr, false);
    }
    return std::make_pair(
        std::shared_ptr<TemporaryFile>(
            new TemporaryFile(file_system, std::move(path))),
        true);
  }

  ~TemporaryFile() {
    auto ignored_result = _file_system.unlink(path);
    // Maybe the file is already gone, or was never created. We don't care
    // enough to make sure to clean up this temporary file.
  }

  std::string path;

 private:
  TemporaryFile(FileSystem &file_system, std::string &&path) throw(IoError)
      : path(std::move(path)),
        _file_system(file_system) {}

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
      Step step,
      const Callback &callback) override {
    if (command.empty() || step.generator() || isConsolePool(step.poolName())) {
      // Empty commands need no tracing, and neither do generator rule steps
      // because their cleanliness is determined only based on inputs and
      // outputs declared in the manifest anyway.
      //
      // Commands run in the console pool are never counted as clean so they
      // don't need tracing either.
      _inner->invoke(command, step, callback);
      return;
    }

    std::string err;
    if (!_trace_server_handle->startServer(&err)) {
      throw IoError("Failed to start shk-trace server: " + err, 0);
    }

    try {
      bool success;
      std::shared_ptr<TemporaryFile> tmp;
      std::tie(tmp, success) = TemporaryFile::make(_file_system, &err);
      if (!success) {
        throw IoError(err, 0);
      }

      std::string escaped_command;
      getShellEscapedString(command, &escaped_command);
      // Here we assume that the generated temporary file path does not contain
      // ' or ". It would be an evil temporary file creation function that would
      // do that.
      _inner->invoke(
          _escaped_shk_trace_cmd + ""
              " -f '" + tmp->path + "'"
              " -c " + escaped_command,
          step,
          [this, tmp, callback](CommandRunner::Result &&result) {
            computeResults(tmp->path, result);
            callback(std::move(result));
          });
    } catch (const IoError &error) {
      _inner->invoke(
          "/bin/echo Failed to create temporary file && exit 1",
          step,
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
