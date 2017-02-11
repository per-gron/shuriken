#include "cmd/dry_run_command_runner.h"

#include <list>

namespace shk {
namespace {

class DryRunCommandRunner : public CommandRunner {
 public:
  void invoke(
      const std::string &command,
      const std::string &pool_name,
      const Callback &callback) override {
    _enqueued_commands.push_back(callback);
  }

  size_t size() const override {
    return _enqueued_commands.size();
  }

  bool canRunMore() const override {
    return true;
  }

  bool runCommands() override {
    for (const auto &callback : _enqueued_commands) {
      callback(Result());
    }
    _enqueued_commands.clear();
    return false;
  }

 private:
  std::list<Callback> _enqueued_commands;
};

}  // anonymous namespace

std::unique_ptr<CommandRunner> makeDryRunCommandRunner() {
  return std::unique_ptr<CommandRunner>(new DryRunCommandRunner());
}

}  // namespace shk
