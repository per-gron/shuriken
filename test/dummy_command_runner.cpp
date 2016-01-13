#include "dummy_command_runner.h"

namespace shk {
namespace {

template<typename Range>
std::string joinPaths(const Range &paths, const std::string sep = ":") {
  std::string result;
  for (const auto &path : paths) {
    result += path.canonicalized();
    result += sep;
  }
  return result;
}

template<typename Iter, typename Out>
void splitPaths(
    Paths &paths,
    const Iter begin,
    const Iter end,
    const typename Iter::value_type sep,
    Out out) {
  auto it = begin;
  while (it != end) {
    const auto next = std::find(it, end, sep);
    if (next == end) {
      break;
    }
    const auto str = std::string(it, next);
    *out++ = paths.get(str);
    it = next + 1;
  }
}

}  // anonymous namespace

namespace detail {

std::pair<std::vector<Path>, std::vector<Path>> splitCommand(
    Paths &paths,
    const std::string &command) {
  std::vector<Path> inputs;
  std::vector<Path> outputs;

  const auto semicolon = std::find(command.begin(), command.end(), ';');
  splitPaths(
      paths,
      command.begin(),
      semicolon,
      ':',
      std::back_inserter(inputs));
  if (semicolon != command.end()) {
    splitPaths(
        paths,
        semicolon + 1,
        command.end(),
        ':',
        std::back_inserter(outputs));
  }

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
