// Copyright 2011 Google Inc. All Rights Reserved.
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

#include "cmd/limited_command_runner.h"

namespace shk {
namespace {

class LimitedCommandRunner : public CommandRunner {
 public:
  LimitedCommandRunner(
      const std::function<double ()> &get_load_average,
      double max_load_average,
      int parallelism,
      std::unique_ptr<CommandRunner> &&inner)
      : _get_load_average(get_load_average),
        _max_load_average(max_load_average),
        _parallelism(parallelism),
        _inner(std::move(inner)) {}

  void invoke(
      const std::string &command,
      const std::string &pool_name,
      const Callback &callback) override {
    _inner->invoke(command, pool_name, callback);
  }

  size_t size() const override {
    return _inner->size();
  }

  bool canRunMore() const override {
    return
        static_cast<int>(size()) < _parallelism && (
            (empty() || _max_load_average <= 0.0f) ||
            _get_load_average() < _max_load_average);
  }

  bool runCommands() override {
    return _inner->runCommands();
  }

 private:
  const std::function<double ()> _get_load_average;
  const double _max_load_average;
  const int _parallelism;
  const std::unique_ptr<CommandRunner> _inner;
};

}  // anonymous namespace

std::unique_ptr<CommandRunner> makeLimitedCommandRunner(
    const std::function<double ()> &get_load_average,
    double max_load_average,
    int parallelism,
    std::unique_ptr<CommandRunner> &&inner) {
  return std::unique_ptr<CommandRunner>(
      new LimitedCommandRunner(
          get_load_average,
          max_load_average,
          parallelism,
          std::move(inner)));
}

}  // namespace shk
