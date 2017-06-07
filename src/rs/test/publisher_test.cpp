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

#include <rs/publisher.h>

namespace shk {

TEST_CASE("Publisher") {
  static_assert(!IsPublisher<int>, "int is not a Publisher");

  SECTION("Publisher type eraser") {
    int cancelled = 0;
    auto callback_pub = MakePublisher([&cancelled](auto &&subscriber) {
      subscriber.OnComplete();
      return MakeSubscription(
          [](ElementCount) {},
          [&cancelled] { cancelled++; });
    });
    const auto pub = Publisher<int, std::string>(std::move(callback_pub));

    SECTION("type traits") {
      static_assert(
          IsPublisher<decltype(pub)>,
          "MakePublisher should return Publisher");
    }

    SECTION("Subscribe") {
      int completed = 0;
      auto sub = pub.Subscribe(MakeSubscriber(
          [](auto &&) {},
          [](std::exception_ptr &&) {},
          [&completed] { completed++; }));

      CHECK(completed == 1);
      CHECK(cancelled == 0);
      sub.Cancel();
      CHECK(cancelled == 1);
    }
  }

  SECTION("Callback MakePublisher") {
    int cancelled = 0;
    auto pub = MakePublisher([&cancelled](auto &&subscriber) mutable {
      subscriber.OnComplete();
      return MakeSubscription(
          [](ElementCount) {},
          [&cancelled] { cancelled++; });
    });

    SECTION("type traits") {
      static_assert(
          IsPublisher<decltype(pub)>,
          "MakePublisher should return Publisher");
    }

    SECTION("Subscribe") {
      int completed = 0;
      auto sub = pub.Subscribe(MakeSubscriber(
          [](auto &&) {},
          [](std::exception_ptr &&) {},
          [&completed] { completed++; }));

      CHECK(completed == 1);
      CHECK(cancelled == 0);
      sub.Cancel();
      CHECK(cancelled == 1);
    }

    SECTION("const Subscribe") {
      int cancelled = 0;
      const auto const_pub = MakePublisher([&cancelled](auto &&subscriber) {
        subscriber.OnComplete();
        return MakeSubscription(
            [](ElementCount) {},
            [&cancelled] { cancelled++; });
      });

      int completed = 0;
      auto sub = const_pub.Subscribe(MakeSubscriber(
          [](auto &&) {},
          [](std::exception_ptr &&) {},
          [&completed] { completed++; }));

      CHECK(completed == 1);
      CHECK(cancelled == 0);
      sub.Cancel();
      CHECK(cancelled == 1);
    }
  }
}

}  // namespace shk
