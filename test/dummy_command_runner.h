#include "command_runner.h"

#include "file_system.h"
#include "path.h"

namespace shk {
namespace detail {

/**
 * Split a command back into its input and output paths.
 *
 * This is exposed for testing purposes.
 */
std::pair<std::vector<Path>, std::vector<Path>> splitCommand(
    Paths &paths,
    const std::string &command);

/**
 * "Run" a given command. This reads the command's input files and writes to its
 * output files in a way that can be checked by
 * DummyCommandRunner::checkCommand.
 *
 * This is exposed for testing purposes.
 */
CommandRunner::Result runCommand(
    Paths &paths,
    FileSystem &file_system,
    const std::string &command);

}  // namespace detail

class DummyCommandRunner : public CommandRunner {
 public:
  DummyCommandRunner(Paths &paths, FileSystem &file_system);

  void invoke(
      const std::string &command,
      UseConsole use_console,
      const Callback &callback) override;

  size_t size() const override;

  bool runCommands() override;

  static std::string constructCommand(
      const std::vector<Path> &inputs,
      const std::vector<Path> &outputs);

  /**
   * Verify that a command has run by looking at the file system and see that
   * the output files of the given are there and have the right contents.
   *
   * Throws an exception when the check fails.
   */
  static void checkCommand(
      Paths &paths, FileSystem &file_system, const std::string &command)
          throw(IoError, std::runtime_error);

 private:
  Paths &_paths;
  FileSystem &_file_system;
  std::vector<std::pair<std::string, Callback>> _enqueued_commands;
};

}  // namespace shk
