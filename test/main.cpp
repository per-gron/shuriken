#define CATCH_CONFIG_MAIN

#include <vector>
#include <algorithm>

#include <catch.hpp>
#include <rapidcheck/catch.h>

TEST_CASE("Basic") {
  rc::prop("double reversal yields the original value", [](
      const std::vector<int> &l0) {
    auto l1 = l0;
    std::reverse(begin(l1), end(l1));
    std::reverse(begin(l1), end(l1));
    RC_ASSERT(l0 == l1);
  });
}
