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

#include <util/string_view.h>

namespace shk {

TEST_CASE("string_view") {
  SECTION("nt_string_view") {
    SECTION("default constructor") {
      nt_string_view v;
      REQUIRE(v.data() != nullptr);
      CHECK(v.data()[0] == 0);
      CHECK(v.size() == 0);
      CHECK(v.null_terminated());
    }

    SECTION("from C string") {
      nt_string_view v("hej");
      REQUIRE(v.data() == "hej");
      CHECK(v.size() == 3);
      CHECK(v.null_terminated());
    }

    SECTION("from C string, cut short") {
      nt_string_view v("hej", 2);
      REQUIRE(v.data() == "hej");
      CHECK(v.size() == 2);
      CHECK(!v.null_terminated());
    }

    SECTION("from C++ string, cut short") {
      std::string str("hej");
      nt_string_view v(str);
      REQUIRE(v.data() == str.data());
      CHECK(v.size() == 3);
      CHECK(v.null_terminated());
    }

    SECTION("std::hash") {
      CHECK(
          std::hash<nt_string_view>()(nt_string_view("")) ==
          std::hash<string_view>()(string_view("")));
    }
  }
}

}  // namespace shk
