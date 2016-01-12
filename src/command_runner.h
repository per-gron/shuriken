#pragma once

#include <string>
#include <vector>

namespace shk {

/**
 * A CommandRunner is responsible for invoking build commands, for detecting
 * which files the command read and wrote to, verifying that the command did
 * not do something disallowed, for example access network or leave a daemon
 * process running.
 */
class CommandRunner {
 public:
  virtual ~CommandRunner() = default;

  struct Result {
    std::vector<std::string> input_files;
    std::vector<std::string> output_files;
    int return_code;
    std::string output;
    std::vector<std::string> linting_errors;
  };

  using Callback = std::function<void (Result &&result)>;

  /**
   * Invoke a command. When the command is finished, callback is invoked with
   * the result. It is allowed to call invoke() and empty() from the callback,
   * but it is not allowed to call wait() from there.
   */
  virtual Result invoke(
      const std::string &command,
      const Callback &callback) = 0;

  /**
   * Returns false if there are any commands running currently.
   */
  virtual bool empty() const;

  /**
   * Wait until a command has completed. If there are no commands running
   * right now (if empty()), then the method returns immediately.
   */
  virtual void wait();
};

}  // namespace shk
