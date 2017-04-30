#include <catch.hpp>

#include "event_info_map.h"

namespace shk {

TEST_CASE("EventInfo") {
  EventInfoMap map;

  map.addEvent(1, 2).pid = 1337;
  map.addEvent(1, 3).pid = 9001;
  map.addEvent(2, 2).pid = 321;

  SECTION("find") {
    SECTION("old event") {
      const auto *evt = map.find(1, 2);
      REQUIRE(evt);
      CHECK(evt->pid == 1337);
    }

    SECTION("new event") {
      const auto *evt = map.find(1, 3);
      REQUIRE(evt);
      CHECK(evt->pid == 9001);
    }

    SECTION("unknown thread") {
      CHECK(map.find(1000, 1) == nullptr);
    }

    SECTION("unknown event") {
      CHECK(map.find(1, 1000) == nullptr);
    }
  }

  SECTION("overwrite") {
    map.addEvent(1, 2);

    const auto *evt = map.find(1, 2);
    REQUIRE(evt);
    CHECK(evt->pid == 0);
  }

  SECTION("findLast") {
    SECTION("missing") {
      CHECK(map.findLast(3) == nullptr);
    }

    SECTION("one write") {
      const auto *evt = map.findLast(2);
      REQUIRE(evt);
      CHECK(evt->pid == 321);
    }

    SECTION("two writes") {
      const auto *evt = map.findLast(1);
      REQUIRE(evt);
      CHECK(evt->pid == 9001);
    }

    SECTION("only event erased") {
      map.erase(2, 2);
      CHECK(map.findLast(2) == nullptr);
    }

    SECTION("newest of two events erased") {
      map.erase(1, 3);
      CHECK(map.findLast(1) == nullptr);
    }

    SECTION("oldest of two events erased") {
      map.erase(1, 2);
      const auto *evt = map.findLast(1);
      REQUIRE(evt);
      CHECK(evt->pid == 9001);
    }
  }

  SECTION("erase") {
    SECTION("gone after") {
      CHECK(map.find(1, 2) != nullptr);
      map.erase(1, 2);
      CHECK(map.find(1, 2) == nullptr);
    }

    SECTION("erase only requested event") {
      map.erase(1, 2);
      CHECK(map.find(1, 3) != nullptr);
      CHECK(map.find(2, 2) != nullptr);
    }

    SECTION("unknown thread") {
      map.erase(3, 1);  // should do nothing
    }

    SECTION("unknown event") {
      map.erase(1, 1000);  // should do nothing
    }

    SECTION("multiple erases") {
      map.erase(1, 2);
      map.erase(1, 2);  // should do nothing
    }
  }

  SECTION("verifyNoEventsForThread") {
    SECTION("failure") {
      CHECK_THROWS(map.verifyNoEventsForThread(1));
      CHECK_THROWS(map.verifyNoEventsForThread(2));
    }

    SECTION("never known thread") {
      map.verifyNoEventsForThread(3);
    }

    SECTION("thread that is no longer present") {
      map.erase(2, 2);
      map.verifyNoEventsForThread(2);
    }
  }
}

}  // namespace shk
