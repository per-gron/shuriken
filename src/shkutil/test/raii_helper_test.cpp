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

#include <util/raii_helper.h>

namespace shk {
namespace {

int gVal = 0;

int *gPtr = nullptr;

void mockFree(int *ptr) {
  CHECK(gPtr == nullptr);
  CHECK(ptr != nullptr);
  gPtr = ptr;
}

void noop(int unused) {
}

void neverCalled(int *ptr) {
  CHECK(false);
}

}  // anonymous namespace

TEST_CASE("RAIIHelper") {
  gPtr = nullptr;

  SECTION("DefaultConstructor") {
    RAIIHelper<int, void, noop, 3> helper;
    CHECK(helper.get() == 3);
  }

  SECTION("InvokesFreeOnDestruction") {
    int an_int = 0;

    {
      RAIIHelper<int *, void, mockFree> helper(&an_int);
    }

    CHECK(gPtr == &an_int);
  }

  SECTION("Reset") {
    SECTION("WorksWhenInitiallyEmpty") {
      RAIIHelper<int *, void, mockFree> helper(nullptr);
      int an_int = 0;
      helper.reset(&an_int);
      CHECK(gPtr == nullptr);
    }

    SECTION("WorksWhenNotInitiallyEmpty") {
      int an_int = 0;
      RAIIHelper<int *, void, mockFree> helper(&an_int);
      helper.reset(nullptr);
      CHECK(gPtr == &an_int);
    }
  }

  SECTION("DoesNotInvokeFreeOnDestructionWhenEmpty") {
    int an_int = 0;
    gPtr = &an_int;

    {
      RAIIHelper<int *, void, mockFree> helper(nullptr);
    }

    CHECK(gPtr == &an_int);
  }

  SECTION("DoesNotInvokeFreeAfterRelease") {
    int an_int = 0;

    {
      RAIIHelper<int *, void, mockFree> helper(&an_int);
      CHECK(helper.release() == &an_int);
    }

    CHECK(gPtr == nullptr);
  }

  SECTION("OperatorBool") {
    int an_int = 0;
    RAIIHelper<int *, void, mockFree, &gVal> empty(&gVal);
    CHECK(!empty);
    RAIIHelper<int *, void, mockFree, &gVal> not_empty(&an_int);
    CHECK(!!not_empty);
  }

  SECTION("EmptyPredicate") {
    SECTION("InvokesFreeOnDestruction") {
      int an_int = 0;

      {
        RAIIHelper<int *, void, mockFree, &gVal> helper(&an_int);
      }

      CHECK(gPtr == &an_int);
    }

    SECTION("DoesNotInvokeFreeOnDestructionWhenEmpty") {
      int an_int = 0;
      gPtr = &an_int;

      {
        RAIIHelper<int *, void, mockFree, &gVal> helper(&gVal);
      }

      CHECK(gPtr == &an_int);
    }
  }

  SECTION("DoesNotInvokeFreeBeforeDestruction") {
    int an_int = 0;
    RAIIHelper<int *, void, mockFree> helper(&an_int);
    CHECK(gPtr == nullptr);
  }

  SECTION("DoesNotInvokeFreeOnNull") {
    RAIIHelper<int *, void, neverCalled> helper(nullptr);
  }

  SECTION("Get") {
    int an_int = 0;
    const RAIIHelper<int *, void, mockFree> helper(&an_int);
    CHECK(helper.get() == &an_int);
  }
}

}  // namespace shk
