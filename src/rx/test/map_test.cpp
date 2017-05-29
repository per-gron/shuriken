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

#include <string>
#include <vector>

#include <rx/iterate.h>
#include <rx/map.h>

#include "test_util.h"

namespace shk {

TEST_CASE("Map") {
  auto add_self = Map([](auto x) { return x + x; });

  SECTION("empty") {
    CHECK(
        GetAll<int>(add_self(Iterate(std::vector<int>{}))) ==
        (std::vector<int>{}));
  }

  SECTION("one int") {
    CHECK(
        GetAll<int>(add_self(Iterate(std::vector<int>{ 1 }))) ==
        (std::vector<int>{ 2 }));
  }

  SECTION("two ints") {
    CHECK(
        GetAll<int>(add_self(Iterate(std::vector<int>{ 1, 5 }))) ==
        (std::vector<int>{ 2, 10 }));
  }

  SECTION("one string") {
    // To check that add_self can be used with different types (it is also used
    // with ints).
    CHECK(
        GetAll<std::string>(
            add_self(Iterate(std::vector<std::string>{ "a" }))) ==
        (std::vector<std::string>{ "aa" }));
  }

  SECTION("request only one") {
    CHECK(
        GetAll<int>(
            add_self(Iterate(std::vector<int>{ 1, 5 })),
            1,
            false) ==
        (std::vector<int>{ 2 }));
  }

  SECTION("request only two") {
    CHECK(
        GetAll<int>(
            add_self(Iterate(std::vector<int>{ 1, 6 })), 2) ==
        (std::vector<int>{ 2, 12 }));
  }
}

}  // namespace shk
