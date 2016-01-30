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

#pragma once

#include <memory>
#include <queue>

#include "build_status.h"
#include "stopwatch.h"

namespace shk {
namespace detail {

class RateInfo {
 public:
  RateInfo() : _rate(-1) {}

  void restart() { _stopwatch.restart(); }
  double elapsed() const { return _stopwatch.elapsed(); }
  double rate() { return _rate; }

  void updateRate(int edges) {
    if (edges) {
      _rate = edges / elapsed();
    }
  }

 private:
  double _rate;
  Stopwatch _stopwatch;
};

class SlidingRateInfo {
 public:
  SlidingRateInfo(int parallelism)
    : _rate(-1), _parallelism(parallelism), _last_update(-1) {}

  void restart() { _stopwatch.restart(); }
  double rate() const { return _rate; }

  void updateRate(int update_hint) {
    if (update_hint == _last_update) {
      return;
    }
    _last_update = update_hint;

    if (_times.size() == _parallelism) {
      _times.pop();
    }
    _times.push(_stopwatch.elapsed());
    if (_times.back() != _times.front()) {
      _rate = _times.size() / (_times.back() - _times.front());
    }
  }

private:
  double _rate;
  Stopwatch _stopwatch;
  const size_t _parallelism;
  std::queue<double> _times;
  int _last_update;
};

}  // namespace detail

/**
 * Create a BuildStatus object that reports the build status to the terminal.
 *
 * This is the main BuildStatus implementation.
 */
std::unique_ptr<BuildStatus> makeTerminalBuildStatus(
    bool verbose,
    int parallelism,
    const char *progress_status_format);

}  // namespace shk
