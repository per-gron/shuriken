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

#include "cmd/dry_run_command_runner.h"

#include <list>

namespace shk {
namespace {

class DryRunCommandRunner : public CommandRunner {
 public:
  void invoke(
      nt_string_view command,
      Step step,
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
