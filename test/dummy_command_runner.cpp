#include "dummy_command_runner.h"

#include "in_memory_file_system.h"

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

std::string makeInputData(
    FileSystem &file_system, const std::vector<Path> &inputs) {
  std::string input_data;
  for (const auto &input : inputs) {
    input_data += input.canonicalized() + "\n";
    input_data += file_system.readFile(input);
    input_data += "\n";
  }
  return input_data;
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

CommandRunner::Result runCommand(
    FileSystem &file_system,
    const std::string &command) {
  CommandRunner::Result result;
  std::tie(result.input_files, result.output_files) =
      splitCommand(file_system.paths(), command);

  std::string input_data;
  try {
    input_data = makeInputData(file_system, result.input_files);
  } catch (IoError &) {
    result.return_code = 1;
    return result;
  }

  for (const auto &output : result.output_files) {
    try {
      writeFile(file_system, output, output.canonicalized() + "\n" + input_data);
    } catch (IoError &) {
      result.return_code = 1;
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
    const Callback &callback) {
  _enqueued_commands.emplace_back(command, callback);
}

size_t DummyCommandRunner::size() const {
  return _enqueued_commands.size();
}

void DummyCommandRunner::runCommands() {
  for (const auto &command : _enqueued_commands) {
    command.second(detail::runCommand(_file_system, command.first));
  }
  _enqueued_commands.clear();
}

std::string DummyCommandRunner::constructCommand(
    const std::vector<Path> &inputs,
    const std::vector<Path> &outputs) {
  return joinPaths(inputs) + ";" + joinPaths(outputs);
}

void DummyCommandRunner::checkCommand(
    FileSystem &file_system, const std::string &command)
        throw(IoError, std::runtime_error) {
  std::vector<Path> inputs;
  std::vector<Path> outputs;
  std::tie(inputs, outputs) = detail::splitCommand(file_system.paths(), command);

  const auto input_data = makeInputData(file_system, inputs);

  for (const auto &output : outputs) {
    const auto data = file_system.readFile(output);
    if (data != output.canonicalized() + "\n" + input_data) {
      throw std::runtime_error("Unexpected output file contents for file " + output.canonicalized());
    }
  }
}

}  // namespace shk
