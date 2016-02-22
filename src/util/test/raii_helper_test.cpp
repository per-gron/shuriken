#include <catch.hpp>

#include <util/raii_helper.h>

namespace util {
namespace {

int gVal = 0;
bool isGValPtr(int *ptr) {
  return ptr != &gVal;
}

int *gPtr = nullptr;

void mockFree(int *ptr) {
  CHECK(gPtr == nullptr);
  CHECK(ptr != nullptr);
  gPtr = ptr;
}

void neverCalled(int *ptr) {
  CHECK(false);
}

}  // anonymous namespace

TEST_CASE("RAIIHelper") {
  gPtr = nullptr;

  SECTION("InvokesFreeOnDestruction") {
    int an_int = 0;

    {
      RAIIHelper<int *, void, mockFree> helper(&an_int);
    }

    CHECK(gPtr == &an_int);
  }

  SECTION("DoesNotInvokeFreeOnDestructionWhenEmpty") {
    int an_int = 0;
    gPtr = &an_int;

    {
      RAIIHelper<int *, void, mockFree> helper(nullptr);
    }

    CHECK(gPtr == &an_int);
  }

  SECTION("EmptyPredicate") {
    SECTION("InvokesFreeOnDestruction") {
      int an_int = 0;

      {
        RAIIHelper<int *, void, mockFree, isGValPtr> helper(&an_int);
      }

      CHECK(gPtr == &an_int);
    }

    SECTION("DoesNotInvokeFreeOnDestructionWhenEmpty") {
      int an_int = 0;
      gPtr = &an_int;

      {
        RAIIHelper<int *, void, mockFree, isGValPtr> helper(&gVal);
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

}  // namespace util
