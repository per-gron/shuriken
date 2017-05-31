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

#include <memory>
#include <vector>

#include <rs/empty.h>
#include <rs/iterate.h>
#include <rs/just.h>
#include <rs/reduce.h>
#include <rs/subscriber.h>
#include <rs/throw.h>

#include "infinite_range.h"
#include "test_util.h"

namespace shk {

TEST_CASE("Reduce") {
  SECTION("Reduce") {
    auto sum = Reduce(100, [](int a, int v) { return a + v; });
    auto fail_on = [](int fail_value, int call_count) {
      return Reduce(100, [fail_value, call_count, times_called = 0](
          int a, int v) mutable {
        CHECK(++times_called <= call_count);

        if (v == fail_value) {
          throw std::runtime_error("fail_on");
        } else {
          return a + v;
        }
      });
    };

    static_assert(
        IsPublisher<decltype(sum(Just(0)))>,
        "Range stream should be a publisher");

    SECTION("empty") {
      CHECK(GetOne<int>(sum(Empty())) == 100);
    }

    SECTION("one value") {
      CHECK(GetOne<int>(sum(Just(1))) == 101);
    }

    SECTION("two values") {
      CHECK(GetOne<int>(sum(Iterate(std::vector<int>{ 1, 2 }))) == 103);
    }

    SECTION("request zero") {
      CHECK(GetOne<int>(sum(Iterate(std::vector<int>{ 1, 2 })), 0) == 0);
    }

    SECTION("request one") {
      CHECK(GetOne<int>(sum(Iterate(std::vector<int>{ 1, 2 })), 1) == 103);
    }

    SECTION("request two") {
      CHECK(GetOne<int>(sum(Iterate(std::vector<int>{ 1, 2 })), 2) == 103);
    }

    SECTION("error on first") {
      auto error = GetError(fail_on(0, 1)(Iterate(std::vector<int>{ 0 })));
      CHECK(GetErrorWhat(error) == "fail_on");
    }

    SECTION("error on first of two") {
      // The reducer functor should be invoked only once
      auto error = GetError(fail_on(0, 1)(Iterate(std::vector<int>{ 0, 1 })));
      CHECK(GetErrorWhat(error) == "fail_on");
    }

    SECTION("error on first of infinite") {
      // This will terminate only if the Reduce operator actually cancels the
      // underlying InfiniteRange stream.
      auto error = GetError(fail_on(0, 1)(InfiniteRange(0)));
      CHECK(GetErrorWhat(error) == "fail_on");
    }

    SECTION("cancel") {
      auto null_subscriber = MakeSubscriber(
          [](int next) { CHECK(!"should not happen"); },
          [](std::exception_ptr &&error) { CHECK(!"should not happen"); },
          [] { CHECK(!"should not happen"); });

      auto sub = sum(InfiniteRange(0)).Subscribe(std::move(null_subscriber));
      sub.Cancel();
      // Because the subscription is cancelled, it should not request values
      // from the infinite range (which would never terminate).
      sub.Request(1);
    }
  }

  SECTION("ReduceGet") {
    SECTION("non-copyable accumulator") {
      auto wrap_in_unique_ptr = ReduceGet(
          [] { return std::unique_ptr<int>(); },
          [](std::unique_ptr<int> &&accum, int val) {
            return std::make_unique<int>(val);
          });
      CHECK(
          *GetOne<std::unique_ptr<int>>(
              wrap_in_unique_ptr(Iterate(std::vector<int>{ 1, 2 }))) == 2);
    }
  }

  SECTION("ReduceWithoutInitial") {
    auto reducer = [](int a, int b) {
      return a * a + b;
    };
    auto reduce = ReduceWithoutInitial<int>(reducer);

    SECTION("empty") {
      auto error = GetError(reduce(Empty()));
      CHECK(
          GetErrorWhat(error) ==
          "ReduceWithoutInitial invoked with empty stream");
    }

    SECTION("single value") {
      CHECK(GetOne<int>(reduce(Just(4))) == 4);
    }

    SECTION("two values") {
      auto values = Iterate(std::vector<int>({ 2, 3 }));
      CHECK(GetOne<int>(reduce(values)) == (2 * 2) + 3);
    }

    SECTION("three values") {
      auto values = Iterate(std::vector<int>({ 2, 3, 4 }));
      int first = (2 * 2) + 3;
      CHECK(GetOne<int>(reduce(values)) == first * first + 4);
    }

    SECTION("failing input stream") {
      auto exception = std::make_exception_ptr(std::runtime_error("test_error"));
      auto error = GetError(reduce(Throw(exception)));
      CHECK(GetErrorWhat(error) == "test_error");
    }
  }
}

}  // namespace shk
