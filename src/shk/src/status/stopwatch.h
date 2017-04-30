// Copyright 2011 Google Inc. All Rights Reserved.
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

#pragma once

#include <chrono>
#include <functional>

namespace shk {

/**
 * A simple stopwatch which returns the time in seconds since restart() was
 * called.
 */
class Stopwatch {
 public:
  Stopwatch(
      const std::function<std::chrono::high_resolution_clock::time_point ()> &clock
          = std::chrono::high_resolution_clock::now)
      : _clock(clock) {}

  /**
   * Seconds since restart() call.
   */
  double elapsed() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        _clock() - _started).count() / 1000.0;
  }

  void restart() { _started = _clock(); }

 private:
  std::function<std::chrono::high_resolution_clock::time_point ()> _clock;
  std::chrono::high_resolution_clock::time_point _started;
};

}  // namespace shk
