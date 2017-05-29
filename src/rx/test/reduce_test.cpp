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

#include <rx/empty.h>
#include <rx/iterate.h>
#include <rx/just.h>
#include <rx/reduce.h>
#include <rx/subscriber.h>

namespace shk {
namespace {

template <typename T, typename Publisher>
T get(const Publisher &publisher, size_t request_count = Subscription::kAll) {
  bool has_value = false;
  bool is_done = false;
  T result{};
  auto sub = publisher(MakeSubscriber(
      [&result, &has_value, &is_done](T &&val) {
        CHECK(!is_done);
        CHECK(!has_value);
        result = std::move(val);
        has_value = true;
      },
      [](std::exception_ptr &&error) { CHECK(!"should not happen"); },
      [&has_value, &is_done] {
        CHECK(!is_done);
        CHECK(has_value);
        is_done = true;
      }));
  CHECK(!has_value);
  CHECK(!is_done);
  sub.Request(request_count);
  CHECK(is_done == (request_count != 0));
  return result;
}

}  // anonymous namespace

TEST_CASE("Reduce") {
  auto sum = Reduce(100, [](int a, int v) { return a + v; });

  SECTION("empty") {
    CHECK(get<int>(sum(Empty())) == 100);
  }

  SECTION("one value") {
    CHECK(get<int>(sum(Just(1))) == 101);
  }

  SECTION("two values") {
    CHECK(get<int>(sum(Iterate(std::vector<int>{ 1, 2 }))) == 103);
  }

  SECTION("request zero") {
    CHECK(get<int>(sum(Iterate(std::vector<int>{ 1, 2 })), 0) == 0);
  }

  SECTION("request one") {
    CHECK(get<int>(sum(Iterate(std::vector<int>{ 1, 2 })), 1) == 103);
  }

  SECTION("request two") {
    CHECK(get<int>(sum(Iterate(std::vector<int>{ 1, 2 })), 2) == 103);
  }
}

}  // namespace shk
