#include "dummy_command_runner.h"

namespace shk {
namespace {

template<typename Range>
std::string joinPaths(const Range &paths, const std::string sep = ":") {
  std::string result;
  for (const auto &path : paths) {
    if (!result.empty()) {
      result += sep;
    }
    result += path.canonicalized();
  }
  return result;
}

}  // anonymous namespace

namespace detail {

std::pair<std::vector<Path>, std::vector<Path>> splitCommand(
    const Paths &paths,
    const std::string &command) {
  std::vector<Path> inputs;
  std::vector<Path> outputs;
  return std::make_pair(inputs, outputs);
}

}  // namespace detail

void DummyCommandRunner::invoke(
    const std::string &command,
    const Callback &callback) {
  _enqueued_commands.emplace_back(command, callback);
}

bool DummyCommandRunner::empty() const {
  return _enqueued_commands.empty();
}

void DummyCommandRunner::runCommands() {
  for (const auto &command : _enqueued_commands) {
    runCommand(command.first, command.second);
  }
  _enqueued_commands.clear();
}

std::string DummyCommandRunner::constructCommand(
    const std::vector<Path> &inputs,
    const std::vector<Path> &outputs) {
  return joinPaths(inputs) + ";" + joinPaths(outputs);
}

bool DummyCommandRunner::checkCommand(
    FileSystem &file_system, const std::string &command) {
  // TODO(peck): Implement me
  return false;
}

void DummyCommandRunner::runCommand(
    const std::string &command, const Callback &callback) {
  // TODO(peck): Implement me
}

}  // namespace shk
