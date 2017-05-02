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

#include "io_error.h"

namespace shk {

TEST_CASE("IoError") {
  SECTION("Construct") {
    SECTION("default constructor") {
      const auto error = IoError();
      CHECK(std::string(error.what()) == "");
      CHECK(error.code() == 0);
      CHECK(!error);
    }

    SECTION("success") {
      const auto error = IoError::success();
      CHECK(std::string(error.what()) == "");
      CHECK(error.code() == 0);
      CHECK(!error);
    }

    SECTION("error constructor") {
      const auto error = IoError("hello", 123);
      CHECK(std::string(error.what()) == "hello");
      CHECK(error.code() == 123);
      CHECK(error);
    }

    SECTION("copy constructor") {
      SECTION("non-error") {
        auto original = std::unique_ptr<IoError>(new IoError());
        const auto copy = *original;
        original.reset();

        CHECK(std::string(copy.what()) == "");
        CHECK(copy.code() == 0);
        CHECK(!copy);
      }

      SECTION("error") {
        auto original = std::unique_ptr<IoError>(new IoError("hello", 123));
        const auto copy = *original;
        original.reset();

        CHECK(std::string(copy.what()) == "hello");
        CHECK(copy.code() == 123);
        CHECK(copy);
      }
    }
  }

  SECTION("Compare") {
    const auto default_constructed = IoError();
    const auto success = IoError::success();
    const auto error_1 = IoError("hello", 123);
    const auto error_2 = IoError("hello", 123);
    const auto error_3 = IoError("hello", 0);
    const auto error_4 = IoError("hello!", 123);

    CHECK(default_constructed == default_constructed);
    CHECK(!(default_constructed != default_constructed));

    CHECK(default_constructed == success);
    CHECK(!(default_constructed != success));

    CHECK(error_1 == error_1);
    CHECK(!(error_1 != error_1));

    CHECK(error_2 == error_2);
    CHECK(!(error_2 != error_2));

    CHECK(!(error_1 == error_3));
    CHECK(error_1 != error_3);

    CHECK(!(error_1 == error_4));
    CHECK(error_1 != error_4);

    CHECK(!(success == error_3));
    CHECK(success != error_3);
  }
}

}  // namespace shk
