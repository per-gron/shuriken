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
    const Paths &paths,
    const std::string &command);

}  // namespace detail

class DummyCommandRunner : public CommandRunner {
 public:
  void invoke(
      const std::string &command,
      const Callback &callback) override;

  bool empty() const override;

  void runCommands() override;

  static std::string constructCommand(
      const std::vector<Path> &inputs,
      const std::vector<Path> &outputs);

  /**
   * Verify that a command has run by looking at the file system and see that
   * the output files of the given are there and have the right contents.
   */
  static bool checkCommand(FileSystem &file_system, const std::string &command);

 private:
  void runCommand(const std::string &command, const Callback &callback);

  std::vector<std::pair<std::string, Callback>> _enqueued_commands;
};

}  // namespace shk
