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

#include <rs/just.h>
#include <rs/take.h>

#include "infinite_range.h"
#include "test_util.h"

namespace shk {

TEST_CASE("Take") {
  SECTION("take from empty") {
    SECTION("take 0") {
      auto stream = Take(0)(Just());
      CHECK(
          GetAll<int>(stream) ==
          (std::vector<int>{}));
      static_assert(
          IsPublisher<decltype(stream)>,
          "Take stream should be a publisher");
    }

    SECTION("take 1") {
      auto stream = Take(1)(Just());
      CHECK(GetAll<int>(stream) == (std::vector<int>{}));
    }

    SECTION("take -1") {
      auto stream = Take(-1)(Just());
      CHECK(GetAll<int>(stream) == (std::vector<int>{}));
    }

    SECTION("take infinite") {
      auto stream = Take(ElementCount::Infinite())(Just());
      CHECK(GetAll<int>(stream) == (std::vector<int>{}));
    }
  }

  SECTION("take from single element") {
    SECTION("take 0") {
      auto stream = Take(0)(Just(1));
      CHECK(GetAll<int>(stream) == (std::vector<int>{}));
    }

    SECTION("take 1") {
      auto stream = Take(1)(Just(1));
      CHECK(GetAll<int>(stream) == (std::vector<int>{ 1 }));
    }

    SECTION("take -1") {
      auto stream = Take(-1)(Just(1));
      CHECK(GetAll<int>(stream) == (std::vector<int>{}));
    }

    SECTION("take infinite") {
      auto stream = Take(ElementCount::Infinite())(Just(1));
      CHECK(GetAll<int>(stream) == (std::vector<int>{ 1 }));
    }
  }

  SECTION("take from multiple elements") {
    SECTION("take 0") {
      auto stream = Take(0)(Just(1, 2, 3));
      CHECK(GetAll<int>(stream) == (std::vector<int>{}));
    }

    SECTION("take 1") {
      auto stream = Take(1)(Just(1, 2, 3));
      CHECK(GetAll<int>(stream) == (std::vector<int>{ 1 }));
    }

    SECTION("take 2") {
      auto stream = Take(2)(Just(1, 2, 3));
      CHECK(GetAll<int>(stream) == (std::vector<int>{ 1, 2 }));
    }

    SECTION("take 3") {
      auto stream = Take(3)(Just(1, 2, 3));
      CHECK(GetAll<int>(stream) == (std::vector<int>{ 1, 2, 3 }));
    }

    SECTION("take 4") {
      auto stream = Take(4)(Just(1, 2, 3));
      CHECK(GetAll<int>(stream) == (std::vector<int>{ 1, 2, 3 }));
    }

    SECTION("take -1") {
      auto stream = Take(-1)(Just(1, 2, 3));
      CHECK(GetAll<int>(stream) == (std::vector<int>{}));
    }

    SECTION("take infinite") {
      auto stream = Take(ElementCount::Infinite())(Just(1, 2, 3));
      CHECK(GetAll<int>(stream) == (std::vector<int>{ 1, 2, 3 }));
    }
  }

  SECTION("use stream multiple times") {
    auto stream = Take(2)(Just(1, 2, 3));
    CHECK(GetAll<int>(stream) == (std::vector<int>{ 1, 2 }));
    CHECK(GetAll<int>(stream) == (std::vector<int>{ 1, 2 }));
  }
}

}  // namespace shk
