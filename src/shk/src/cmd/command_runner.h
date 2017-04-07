#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "dependency_type.h"
#include "exit_status.h"

namespace shk {

/**
 * A CommandRunner is responsible for invoking build commands, for detecting
 * which files the command read and wrote to, verifying that the command did
 * not do something disallowed, for example access network or leave a daemon
 * process running.
 *
 * It is not responsible for verifying anything that requires knowledge of the
 * whole build graph to check, for example if the command read a file that is
 * an output of another command without declaring that as a dependency.
 */
class CommandRunner {
 public:
  virtual ~CommandRunner() = default;

  struct Result {
    /**
     * Input files are paths to files that the program read as inputs. If the
     * path is to a symlink, it means that the program depends on the contents
     * of that symlink. To indicate that a program read through a symlink, both
     * the symlink and the path pointed to should be in the input files list.
     */
    std::unordered_map<std::string, DependencyType> input_files;
    /**
     * Output files are files that the program created as output of its work.
     * They have the same semantics as input files wrt symlinks etc.
     */
    std::unordered_set<std::string> output_files;
    ExitStatus exit_status = ExitStatus::SUCCESS;
    std::string output;
  };

  using Callback = std::function<void (Result &&result)>;

  static void noopCallback(Result &&result) {}

  /**
   * Invoke a command. When the command is finished, callback is invoked with
   * the result. It is allowed to call invoke() and empty() from the callback,
   * but it is not allowed to call runCommands() from there.
   *
   * It is legal to call invoke with an empty command string. That should
   * act as if it executed a command that does nothing.
   *
   * It is legal to call invoke even from a callback of invoke (ie within a
   * runCommands invocation).
   *
   * The callback is always invoked from within a runCommands call. If the
   * CommandRunner object is destroyed before all commands have been run,
   * potential resources should be cleaned up but the callback is not invoked.
   * To ensure that all callbacks are invoked, runCommands() must be called
   * until the CommandRunner is empty().
   */
  virtual void invoke(
      const std::string &command,
      const std::string &pool_name,
      const Callback &callback) = 0;

  /**
   * Returns the number of currently running commands, not including commands
   * that have finished running but haven't yet been "reaped" by runCommands.
   * This means that it is possible to look at size() from a Callback to decide
   * if it is appropriate to run more commands, if that depends on the number
   * of currently running commands.
   */
  virtual size_t size() const = 0;

  bool empty() const { return size() == 0; }

  virtual bool canRunMore() const = 0;

  /**
   * Wait until a command has completed. If there are no commands running
   * right now (if empty()), then the method returns immediately.
   *
   * @return true if the process was interrupted while running the commands.
   */
  virtual bool runCommands() = 0;
};

}  // namespace shk
