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

#include <string>

#include "cache/interner.h"

namespace {

class ConstructDestructCounter {
 public:
  ConstructDestructCounter(int *construct_counter, int *destruct_counter)
      : _construct_counter(construct_counter),
        _destruct_counter(destruct_counter) {
    (*_construct_counter)++;
  }

  ConstructDestructCounter(const ConstructDestructCounter &other)
      : ConstructDestructCounter(
            other._construct_counter, other._destruct_counter) {}

  ConstructDestructCounter &operator=(
      const ConstructDestructCounter &) = delete;

  ~ConstructDestructCounter() {
    (*_destruct_counter)++;
  }

 private:
  int * const _construct_counter;
  int * const _destruct_counter;
};

inline bool operator==(
    const ConstructDestructCounter &a, const ConstructDestructCounter &b) {
  // All ConstructDestructCounter objects are equal
  return true;
}

}  // anonymous namespace

namespace std {

template<>
struct hash<ConstructDestructCounter> {
  using argument_type = ConstructDestructCounter;
  using result_type = std::size_t;

  result_type operator()(const argument_type &h) const {
    return 0;
  }
};

}  // namespace std

namespace shk {

TEST_CASE("Interner") {
  SECTION("constructor") {
    Interner<std::string> interner;
  }

  SECTION("get") {
    SECTION("returns equal value") {
      Interner<std::string> interner;
      CHECK(interner.get("abc") == "abc");
    }

    SECTION("returns same value") {
      Interner<std::string> interner;

      const auto &a = interner.get("abc");
      const auto &b = interner.get("abc");
      CHECK(&a == &b);
    }

    SECTION("copy when not already present") {
      int construct_count = 0;
      int destruct_count = 0;

      {
        ConstructDestructCounter counter(&construct_count, &destruct_count);

        // Sanity check
        CHECK(construct_count == 1);
        CHECK(destruct_count == 0);

        Interner<ConstructDestructCounter> interner;
        interner.get(counter);
        CHECK(construct_count == 2);
        CHECK(destruct_count == 0);
      }
    }
  }

  SECTION("destructor") {
    int construct_count = 0;
    int destruct_count = 0;

    {
      ConstructDestructCounter counter(&construct_count, &destruct_count);

      // Sanity check
      CHECK(construct_count == 1);
      CHECK(destruct_count == 0);

      {
        Interner<ConstructDestructCounter> interner;
        interner.get(counter);
        CHECK(construct_count - destruct_count == 2);
      }
      CHECK(construct_count - destruct_count == 1);
    }
    CHECK(construct_count - destruct_count == 0);
  }
}

}  // namespace shk
