#include "dummy_command_runner.h"

#include "in_memory_file_system.h"

namespace shk {
namespace {

template<typename Range>
std::string joinPaths(const Range &paths, const std::string sep = ":") {
  std::string result;
  for (const auto &path : paths) {
    result += path;
    result += sep;
  }
  return result;
}

template<typename Iter, typename Out>
void splitPaths(
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
    *out++ = std::string(it, next);
    it = next + 1;
  }
}

std::string makeInputData(
    FileSystem &file_system, const std::vector<std::string> &inputs) {
  std::string input_data;
  for (const auto &input : inputs) {
    input_data += input + "\n";
    input_data += file_system.readFile(input);
    input_data += "\n";
  }
  return input_data;
}

}  // anonymous namespace

namespace detail {

std::pair<std::vector<std::string>, std::vector<std::string>> splitCommand(
    const std::string &command) {
  std::vector<std::string> inputs;
  std::vector<std::string> outputs;

  const auto semicolon = std::find(command.begin(), command.end(), ';');
  splitPaths(
      command.begin(),
      semicolon,
      ':',
      std::back_inserter(inputs));
  if (semicolon != command.end()) {
    splitPaths(
        semicolon + 1,
        command.end(),
        ':',
        std::back_inserter(outputs));
  }

  return std::make_pair(inputs, outputs);
}

CommandRunner::Result runCommand(
    FileSystem &file_system,
    const std::string &command) {
  CommandRunner::Result result;
  std::tie(result.input_files, result.output_files) = splitCommand(command);

  std::string input_data;
  try {
    input_data = makeInputData(file_system, result.input_files);
  } catch (IoError &) {
    result.exit_status = ExitStatus::FAILURE;
    return result;
  }

  for (const auto &output : result.output_files) {
    try {
      file_system.writeFile(
          output,
          output + "\n" + input_data);
    } catch (IoError &) {
      result.exit_status = ExitStatus::FAILURE;
      return result;
    }
  }

  return result;
}

}  // namespace detail

DummyCommandRunner::DummyCommandRunner(FileSystem &file_system)
    : _file_system(file_system) {}

void DummyCommandRunner::invoke(
    const std::string &command,
    UseConsole use_console,
    const Callback &callback) {
  _enqueued_commands.emplace_back(command, callback);
}

size_t DummyCommandRunner::size() const {
  return _enqueued_commands.size();
}

bool DummyCommandRunner::canRunMore() const {
  return true;
}

bool DummyCommandRunner::runCommands() {
  for (const auto &command : _enqueued_commands) {
    command.second(detail::runCommand(_file_system, command.first));
  }
  _enqueued_commands.clear();
  return false;
}

std::string DummyCommandRunner::constructCommand(
    const std::vector<std::string> &inputs,
    const std::vector<std::string> &outputs) {
  return joinPaths(inputs) + ";" + joinPaths(outputs);
}

void DummyCommandRunner::checkCommand(
    FileSystem &file_system, const std::string &command)
        throw(IoError, std::runtime_error) {
  std::vector<std::string> inputs;
  std::vector<std::string> outputs;
  std::tie(inputs, outputs) = detail::splitCommand(command);

  const auto input_data = makeInputData(file_system, inputs);

  for (const auto &output : outputs) {
    const auto data = file_system.readFile(output);
    if (data != output + "\n" + input_data) {
      throw std::runtime_error("Unexpected output file contents for file " + output);
    }
  }
}

}  // namespace shk
