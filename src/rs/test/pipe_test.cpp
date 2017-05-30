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

#include <rs/pipe.h>

namespace shk {

TEST_CASE("Pipe") {
  SECTION("Empty pipe") {
    CHECK(Pipe()(13) == 13);
  }

  SECTION("Pipe with single element") {
    CHECK(Pipe([](int x) { return x + 2; })(13) == 15);
  }

  SECTION("Pipe with two elements") {
    auto pipe = Pipe(
        [](int x) { return x * x; },
        [](int x) { return x + x; });

    CHECK(pipe(3) == (3 * 3) + (3 * 3));
  }

  SECTION("Pipe with varying types") {
    auto pipe = Pipe(
        [](int x) { return std::to_string(x); },
        [](std::string x) { return x + x; });

    CHECK(pipe(3) == "33");
  }

  SECTION("const pipe") {
    const auto pipe = Pipe([](int x) { return x + 2; });
    CHECK(pipe(3) == 5);
  }

  SECTION("mutable pipe") {
    auto pipe = Pipe([v = 1](int x) mutable { return x + (v++); });
    CHECK(pipe(3) == 4);
    CHECK(pipe(3) == 5);
  }

  SECTION("pipe should own its callback") {
    // This can go wrong if you forget to use std::decay

    auto cb = [v = 1](int x) mutable { return x + (v++); };
    auto pipe = Pipe(cb);
    CHECK(cb(3) == 4);
    CHECK(pipe(3) == 4);
  }
}

}  // namespace shk
