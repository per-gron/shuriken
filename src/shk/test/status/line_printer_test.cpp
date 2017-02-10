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

#include "status/line_printer.h"

namespace shk {

TEST_CASE("LinePrinter") {
  SECTION("elideMiddle") {
    SECTION("NothingToElide") {
      std::string input = "Nothing to elide in this short string.";
      CHECK(input == detail::elideMiddle(input, 80));
    }

    SECTION("ElideInTheMiddle") {
      std::string input = "01234567890123456789";
      CHECK("012...789" == detail::elideMiddle(input, 10));
    }
  }
}

}  // namespace shk
