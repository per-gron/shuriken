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

#include <rs/subscriber.h>

#include "test_util.h"

namespace shk {

TEST_CASE("Subscriber") {
  SECTION("Subscriber the type eraser") {
    SECTION("single type") {
      SECTION("type traits") {
        auto sub = Subscriber<int>(MakeSubscriber(
            [](auto &&) {}, [](std::exception_ptr &&) {}, [] {}));
        static_assert(IsSubscriber<decltype(sub)>, "Should be Subscriber");
      }

      SECTION("move") {
        auto sub = Subscriber<int>(MakeSubscriber(
            [](auto &&) {}, [](std::exception_ptr &&) {}, [] {}));
        auto moved_sub = std::move(sub);
      }

      SECTION("OnNext") {
        int invocations = 0;
        {
          auto sub = Subscriber<int>(MakeSubscriber(
              [&invocations](int val) {
                CHECK(val == 1337);
                invocations++;
              },
              [](std::exception_ptr &&) {
                CHECK(!"OnError should not be called");
              },
              [] {
                CHECK(!"OnComplete should not be called");
              }));
          CHECK(invocations == 0);
          sub.OnNext(1337);
          CHECK(invocations == 1);
        }
        CHECK(invocations == 1);
      }

      SECTION("OnError") {
        int invocations = 0;
        {
          auto sub = Subscriber<int>(MakeSubscriber(
              [](int val) {
                CHECK(!"OnNext should not be called");
              },
              [&invocations](std::exception_ptr &&error) {
                CHECK(GetErrorWhat(error) == "test_error");
                invocations++;
              },
              [] {
                CHECK(!"OnComplete should not be called");
              }));
          CHECK(invocations == 0);
          sub.OnError(std::make_exception_ptr(
              std::runtime_error("test_error")));
          CHECK(invocations == 1);
        }
        CHECK(invocations == 1);
      }

      SECTION("OnComplete") {
        int invocations = 0;
        {
          auto sub = Subscriber<int>(MakeSubscriber(
              [](int val) {
                CHECK(!"OnNext should not be called");
              },
              [](std::exception_ptr &&error) {
                CHECK(!"OnError should not be called");
              },
              [&invocations] {
                invocations++;
              }));
          CHECK(invocations == 0);
          sub.OnComplete();
          CHECK(invocations == 1);
        }
        CHECK(invocations == 1);
      }
    }

    SECTION("two types") {
      SECTION("type traits") {
        auto sub = Subscriber<int, std::string>(MakeSubscriber(
            [](auto &&) {}, [](std::exception_ptr &&) {}, [] {}));
        static_assert(IsSubscriber<decltype(sub)>, "Should be Subscriber");
      }

      SECTION("move") {
        auto sub = Subscriber<int, std::string>(MakeSubscriber(
            [](auto &&) {}, [](std::exception_ptr &&) {}, [] {}));
        auto moved_sub = std::move(sub);
      }

      SECTION("OnNext") {
        int invocations = 0;
        {
          auto sub = Subscriber<int, std::string>(MakeSubscriber(
              [&invocations](auto &&val) {
                invocations++;
              },
              [](std::exception_ptr &&) {
                CHECK(!"OnError should not be called");
              },
              [] {
                CHECK(!"OnComplete should not be called");
              }));
          CHECK(invocations == 0);
          sub.OnNext(1337);
          CHECK(invocations == 1);
          sub.OnNext(std::string("hej"));
          CHECK(invocations == 2);
          sub.OnNext("hej");  // Test implicit conversion
          CHECK(invocations == 3);
        }
        CHECK(invocations == 3);
      }

      SECTION("OnError") {
        int invocations = 0;
        {
          auto sub = Subscriber<int, std::string>(MakeSubscriber(
              [](auto &&val) {
                CHECK(!"OnNext should not be called");
              },
              [&invocations](std::exception_ptr &&error) {
                CHECK(GetErrorWhat(error) == "test_error");
                invocations++;
              },
              [] {
                CHECK(!"OnComplete should not be called");
              }));
          CHECK(invocations == 0);
          sub.OnError(std::make_exception_ptr(
              std::runtime_error("test_error")));
          CHECK(invocations == 1);
        }
        CHECK(invocations == 1);
      }

      SECTION("OnComplete") {
        int invocations = 0;
        {
          auto sub = Subscriber<int, std::string>(MakeSubscriber(
              [](auto &&val) {
                CHECK(!"OnNext should not be called");
              },
              [](std::exception_ptr &&error) {
                CHECK(!"OnError should not be called");
              },
              [&invocations] {
                invocations++;
              }));
          CHECK(invocations == 0);
          sub.OnComplete();
          CHECK(invocations == 1);
        }
        CHECK(invocations == 1);
      }
    }
  }

  SECTION("Empty MakeSubscriber") {
    auto sub = MakeSubscriber();
    static_assert(IsSubscriber<decltype(sub)>, "Should be Subscriber");
    sub.OnNext(1);
    sub.OnNext("hello");
    sub.OnError(std::make_exception_ptr(std::runtime_error("hello")));
    sub.OnComplete();
  }

  SECTION("Callback MakeSubscriber") {
    SECTION("type traits") {
      auto sub = MakeSubscriber(
          [](auto &&) {}, [](std::exception_ptr &&) {}, [] {});
      static_assert(IsSubscriber<decltype(sub)>, "Should be Subscriber");
    }

    SECTION("move") {
      auto sub = MakeSubscriber(
          [](auto &&) {}, [](std::exception_ptr &&) {}, [] {});
      auto moved_sub = std::move(sub);
    }

    SECTION("OnNext") {
      int invocations = 0;
      {
        auto sub = MakeSubscriber(
            [&invocations](int val) {
              CHECK(val == 1337);
              invocations++;
            },
            [](std::exception_ptr &&) {
              CHECK(!"OnError should not be called");
            },
            [] {
              CHECK(!"OnComplete should not be called");
            });
        CHECK(invocations == 0);
        sub.OnNext(1337);
        CHECK(invocations == 1);
      }
      CHECK(invocations == 1);
    }

    SECTION("OnError") {
      int invocations = 0;
      {
        auto sub = MakeSubscriber(
            [](int val) {
              CHECK(!"OnNext should not be called");
            },
            [&invocations](std::exception_ptr &&error) {
              CHECK(GetErrorWhat(error) == "test_error");
              invocations++;
            },
            [] {
              CHECK(!"OnComplete should not be called");
            });
        CHECK(invocations == 0);
        sub.OnError(std::make_exception_ptr(std::runtime_error("test_error")));
        CHECK(invocations == 1);
      }
      CHECK(invocations == 1);
    }

    SECTION("OnComplete") {
      int invocations = 0;
      {
        auto sub = MakeSubscriber(
            [](int val) {
              CHECK(!"OnNext should not be called");
            },
            [](std::exception_ptr &&error) {
              CHECK(!"OnError should not be called");
            },
            [&invocations] {
              invocations++;
            });
        CHECK(invocations == 0);
        sub.OnComplete();
        CHECK(invocations == 1);
      }
      CHECK(invocations == 1);
    }
  }

  SECTION("shared_ptr MakeSubscriber") {
    SECTION("type traits") {
      auto callback_sub = MakeSubscriber(
          [](auto &&) {}, [](std::exception_ptr &&) {}, [] {});
      auto sub = MakeSubscriber(
          std::make_shared<decltype(callback_sub)>(std::move(callback_sub)));
      static_assert(IsSubscriber<decltype(sub)>, "Should be Subscriber");
    }

    SECTION("move") {
      auto callback_sub = MakeSubscriber(
          [](auto &&) {}, [](std::exception_ptr &&) {}, [] {});
      auto sub = MakeSubscriber(
          std::make_shared<decltype(callback_sub)>(std::move(callback_sub)));
      auto moved_sub = std::move(sub);
    }

    SECTION("OnNext") {
      int invocations = 0;
      {
        auto callback_sub = MakeSubscriber(
            [&invocations](int val) {
              CHECK(val == 1337);
              invocations++;
            },
            [](std::exception_ptr &&) {
              CHECK(!"OnError should not be called");
            },
            [] {
              CHECK(!"OnComplete should not be called");
            });
        auto sub = MakeSubscriber(
            std::make_shared<decltype(callback_sub)>(std::move(callback_sub)));
        CHECK(invocations == 0);
        sub.OnNext(1337);
        CHECK(invocations == 1);
      }
      CHECK(invocations == 1);
    }

    SECTION("OnError") {
      int invocations = 0;
      {
        auto callback_sub = MakeSubscriber(
            [](int val) {
              CHECK(!"OnNext should not be called");
            },
            [&invocations](std::exception_ptr &&error) {
              CHECK(GetErrorWhat(error) == "test_error");
              invocations++;
            },
            [] {
              CHECK(!"OnComplete should not be called");
            });
        auto sub = MakeSubscriber(
            std::make_shared<decltype(callback_sub)>(std::move(callback_sub)));
        CHECK(invocations == 0);
        sub.OnError(std::make_exception_ptr(std::runtime_error("test_error")));
        CHECK(invocations == 1);
      }
      CHECK(invocations == 1);
    }

    SECTION("OnComplete") {
      int invocations = 0;
      {
        auto callback_sub = MakeSubscriber(
            [](int val) {
              CHECK(!"OnNext should not be called");
            },
            [](std::exception_ptr &&error) {
              CHECK(!"OnError should not be called");
            },
            [&invocations] {
              invocations++;
            });
        auto sub = MakeSubscriber(
            std::make_shared<decltype(callback_sub)>(std::move(callback_sub)));
        CHECK(invocations == 0);
        sub.OnComplete();
        CHECK(invocations == 1);
      }
      CHECK(invocations == 1);
    }
  }
}

}  // namespace shk
