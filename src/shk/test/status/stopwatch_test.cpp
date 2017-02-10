#include <catch.hpp>

#include "status/stopwatch.h"

namespace shk {

TEST_CASE("Stopwatch") {
  using hrc = std::chrono::high_resolution_clock;

  uint64_t time = 0;
  Stopwatch watch([&time] {
    return hrc::time_point(hrc::duration(time));
  });

  SECTION("Initial") {
    CHECK(watch.elapsed() == 0);
  }

  SECTION("Elapsed") {
    time = 123 * hrc::period::den;
    CHECK(watch.elapsed() == 123);
  }

  SECTION("Restart") {
    time = 123 * hrc::period::den;
    watch.restart();
    time = 223 * hrc::period::den;
    CHECK(watch.elapsed() == 100);
  }
}

}  // namespace shk
