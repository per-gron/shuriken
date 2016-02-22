#define CATCH_CONFIG_MAIN
#include <catch.hpp>

#include "traceexec.h"

namespace traceexec {

TEST_CASE("Version") {
  const auto version = getKextVersion(openSocketNoVersionCheck());
  CHECK(version.major == 1);
  CHECK(version.minor == 0);
  CHECK(version.micro == 0);
}

}  // namespace shk
