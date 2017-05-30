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

#include <vector>

#include <rs/repeat.h>

#include "test_util.h"

namespace shk {

TEST_CASE("Repeat") {
  SECTION("construct") {
    auto stream = Repeat(0, 0);
  }

  SECTION("empty repeat") {
    auto stream = Repeat(1, 0);
    CHECK(GetAll<int>(stream) == std::vector<int>({}));
  }

  SECTION("repeat with one value") {
    auto stream = Repeat(13, 1);
    CHECK(GetAll<int>(stream) == std::vector<int>({ 13 }));
  }

  SECTION("repeat with two values") {
    auto stream = Repeat(15, 2);
    CHECK(GetAll<int>(stream) == std::vector<int>({ 15, 15 }));
  }

  SECTION("don't take reference to its parameter") {
    // Can happen if you forget to std::decay

    int val = 13;
    auto stream = Repeat(val, 1);
    val++;
    CHECK(GetAll<int>(stream) == std::vector<int>({ 13 }));
  }
}

}  // namespace shk
