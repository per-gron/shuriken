#define CATCH_CONFIG_MAIN
#include <catch.hpp>

#include "traceexec.h"

namespace traceexec {

TEST_CASE("Version") {
  SECTION("isCompatible") {
    CHECK(Version(1, 2, 0).isCompatible(1, 0) == true);
    CHECK(Version(1, 2, 0).isCompatible(1, 2) == true);
    CHECK(Version(1, 2, 0).isCompatible(1, 3) == false);
    CHECK(Version(1, 2, 0).isCompatible(2, 0) == false);
    CHECK(Version(2, 0, 0).isCompatible(1, 0) == false);
  }

  SECTION("getKextVersion") {
    const auto version = getKextVersion(openSocketNoVersionCheck());
    CHECK(version.major == 1);
    CHECK(version.minor == 0);
    CHECK(version.micro == 0);
  }
}

}  // namespace shk
