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

#include <catch.hpp>

#include "edit_distance.h"

namespace shk {

TEST_CASE("editDistance") {
  SECTION("empty") {
    CHECK(5 == editDistance("", "ninja"));
    CHECK(5 == editDistance("ninja", ""));
    CHECK(0 == editDistance("", ""));
  }

  SECTION("maxDistance") {
    const bool allow_replacements = true;
    for (int max_distance = 1; max_distance < 7; ++max_distance) {
      const auto distance = editDistance(
          "abcdefghijklmnop", "ponmlkjihgfedcba",
          allow_replacements, max_distance);
      CHECK((max_distance + 1) == distance);
    }
  }

  SECTION("allowReplacements") {
    bool allow_replacements = true;
    CHECK(1 == editDistance("ninja", "njnja", allow_replacements));
    CHECK(1 == editDistance("njnja", "ninja", allow_replacements));

    allow_replacements = false;
    CHECK(2 == editDistance("ninja", "njnja", allow_replacements));
    CHECK(2 == editDistance("njnja", "ninja", allow_replacements));
  }

  SECTION("basics") {
    CHECK(0 == editDistance("browser_tests", "browser_tests"));
    CHECK(1 == editDistance("browser_test", "browser_tests"));
    CHECK(1 == editDistance("browser_tests", "browser_test"));
  }
}

}  // namespace shk
