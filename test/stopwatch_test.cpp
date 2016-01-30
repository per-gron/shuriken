#include <catch.hpp>

#include "stopwatch.h"

namespace shk {

TEST_CASE("Stopwatch") {
  using hrc = std::chrono::high_resolution_clock;

  int time = 0;
  Stopwatch watch([&time] {
    return hrc::time_point(hrc::duration(time));
  });

  SECTION("Initial") {
    CHECK(watch.elapsed().count() == 0);
  }

  SECTION("Elapsed") {
    time = 123;
    CHECK(watch.elapsed().count() == 123);
  }

  SECTION("Restart") {
    time = 123;
    watch.restart();
    time = 223;
    CHECK(watch.elapsed().count() == 100);
  }
}

}  // namespace shk
