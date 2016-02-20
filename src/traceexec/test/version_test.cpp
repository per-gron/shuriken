#include <catch.hpp>

#include "version.h"

namespace traceexec {

TEST_CASE("Version") {
  const auto version = getKextVersion();
  CHECK(version.major == 1);
  CHECK(version.minor == 0);
  CHECK(version.micro == 0);
}

}  // namespace shk
