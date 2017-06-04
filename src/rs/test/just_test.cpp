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

#include <rs/just.h>
#include <rs/subscriber.h>

namespace shk {

TEST_CASE("Just") {
  const auto inert_subscriber = [] {
    return MakeSubscriber(
        [](int next) { CHECK(!"should not happen"); },
        [](std::exception_ptr &&error) { CHECK(!"should not happen"); },
        [] { CHECK(!"should not happen"); });
  };

  const auto counting_subscriber = [](int *nexts, int *finishes) {
    return MakeSubscriber(
        [nexts](int next) { (*nexts)++; },
        [](std::exception_ptr &&error) { CHECK(!"should not happen"); },
        [finishes, nexts] {
          CHECK(*nexts == 1);
          (*finishes)++;
        });
  };

  SECTION("construct") {
    auto stream = Just(1);
    static_assert(
        IsPublisher<decltype(stream)>,
        "Just stream should be a publisher");
  }

  SECTION("just subscribe") {
    auto stream = Just(1);

    stream.Subscribe(inert_subscriber());
  }

  SECTION("request 0") {
    auto stream = Just(1);
    auto sub = stream.Subscribe(inert_subscriber());
    sub.Request(ElementCount(0));
  }

  SECTION("request 1") {
    int nexts = 0;
    int finishes = 0;

    auto stream = Just(1);

    auto sub = stream.Subscribe(counting_subscriber(&nexts, &finishes));
    CHECK(nexts == 0);
    CHECK(finishes == 0);

    sub.Request(ElementCount(1));
    CHECK(nexts == 1);
    CHECK(finishes == 1);
  }

  SECTION("request more") {
    static const ElementCount counts[] = {
        ElementCount(2),
        ElementCount(3),
        ElementCount(5),
        ElementCount::Infinite() };
    for (auto count : counts) {
      int nexts = 0;
      int finishes = 0;

      auto stream = Just(1);

      auto sub = stream.Subscribe(counting_subscriber(&nexts, &finishes));
      CHECK(nexts == 0);
      CHECK(finishes == 0);

      sub.Request(count);
      CHECK(nexts == 1);
      CHECK(finishes == 1);
    }
  }

  SECTION("request twice") {
    int nexts = 0;
    int finishes = 0;

    auto stream = Just(1);

    auto sub = stream.Subscribe(counting_subscriber(&nexts, &finishes));
    CHECK(nexts == 0);
    CHECK(finishes == 0);

    sub.Request(ElementCount(1));
    CHECK(nexts == 1);
    CHECK(finishes == 1);

    sub.Request(ElementCount(1));
    CHECK(nexts == 1);
    CHECK(finishes == 1);
  }
}

}  // namespace shk
