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

#include <catch.hpp>

#include "status/stopwatch.h"

namespace shk {

TEST_CASE("Stopwatch") {
  using hrc = std::chrono::high_resolution_clock;

  uint64_t time = 0;
  Stopwatch watch([&time] {
    return hrc::time_point(hrc::duration(time));
  });

  SECTION("Initial") {
    CHECK(watch.elapsed() == 0);
  }

  SECTION("Elapsed") {
    time = 123 * hrc::period::den;
    CHECK(watch.elapsed() == 123);
  }

  SECTION("Restart") {
    time = 123 * hrc::period::den;
    watch.restart();
    time = 223 * hrc::period::den;
    CHECK(watch.elapsed() == 100);
  }
}

}  // namespace shk
