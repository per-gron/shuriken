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

#include <rx/error.h>
#include <rx/subscriber.h>

namespace shk {

TEST_CASE("Error") {
  auto error = std::make_exception_ptr(std::runtime_error("test"));

  SECTION("construct") {
    auto stream = Error(error);
  }

  SECTION("subscribe") {
    auto stream = Error(error);

    std::exception_ptr received_error;
    auto subscription = stream(MakeSubscriber(
        [](int next) { CHECK(!"should not happen"); },
        [&received_error](std::exception_ptr &&error) {
          received_error = error;
        },
        [] { CHECK(!"should not happen"); }));
    CHECK(received_error);

    received_error = std::exception_ptr();
    subscription.Request(0);
    subscription.Request(1);
    subscription.Request(Subscription::kAll);
    CHECK(!received_error);

    subscription.Cancel();
    CHECK(!received_error);
  }
}

}  // namespace shk
