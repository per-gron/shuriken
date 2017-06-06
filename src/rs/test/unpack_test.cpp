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

#include <rs/unpack.h>

namespace shk {

TEST_CASE("Unpack") {
  SECTION("empty") {
    int called = 0;
    Unpack([&called] { called++; })(std::make_tuple());
    CHECK(called == 1);
  }

  SECTION("with mutable callback") {
    int called = 0;
    Unpack([&called]() mutable { called++; })(std::make_tuple());
    CHECK(called == 1);
  }

  SECTION("with single value") {
    int called = 0;
    Unpack([&called](int val) {
      CHECK(val == 1);
      called++;
    })(std::make_tuple(1));
    CHECK(called == 1);
  }

  SECTION("with two values") {
    int called = 0;
    Unpack([&called](int val, const std::string &str) {
      CHECK(val == 1);
      CHECK(str == "hej");
      called++;
    })(std::make_tuple(1, "hej"));
    CHECK(called == 1);
  }

  SECTION("with pair") {
    int called = 0;
    Unpack([&called](int val, const std::string &str) {
      CHECK(val == 1);
      CHECK(str == "hej");
      called++;
    })(std::make_pair(1, "hej"));
    CHECK(called == 1);
  }

  SECTION("with lvalue reference") {
    auto a_tuple = std::make_tuple(1);
    int called = 0;
    Unpack([&called](int val) {
      CHECK(val == 1);
      called++;
    })(a_tuple);
    CHECK(called == 1);
  }

  SECTION("with const lvalue reference") {
    const auto a_tuple = std::make_tuple(1);
    int called = 0;
    Unpack([&called](const int &val) {
      CHECK(val == 1);
      called++;
    })(a_tuple);
    CHECK(called == 1);
  }

  SECTION("copyable") {
    const auto a_tuple = std::make_tuple(1);
    int called = 0;
    auto unpack = Unpack([&called](const int &val) {
      CHECK(val == 1);
      called++;
    });
    auto unpack_2 = unpack;
  }

  SECTION("with return value") {
    int square = Unpack([](int val) {
      return val * val;
    })(std::make_tuple(3));
    CHECK(square == 9);
  }
}

}  // namespace shk
