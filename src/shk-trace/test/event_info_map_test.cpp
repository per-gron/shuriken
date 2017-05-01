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

#include "event_info_map.h"

namespace shk {

TEST_CASE("EventInfoMap") {
  EventInfoMap map;

  map.newThread(1);
  map.addEvent(1, 2)->pid = 1337;
  map.addEvent(1, 3)->pid = 9001;
  map.newThread(2);
  map.addEvent(2, 2)->pid = 321;

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

  SECTION("addEvent") {
    SECTION("overwrite") {
      CHECK(map.addEvent(1, 2) != nullptr);

      const auto *evt = map.find(1, 2);
      REQUIRE(evt);
      CHECK(evt->pid == 0);
    }

    SECTION("thread that is not traced") {
      CHECK(map.addEvent(1000, 2) == nullptr);
    }
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
    SECTION("spawn already existing thread") {
      CHECK_THROWS(map.newThread(1));
      CHECK_THROWS(map.newThread(2));
    }

    SECTION("terminate thread with outstanding events") {
      CHECK_THROWS(map.terminateThread(1));
      CHECK_THROWS(map.terminateThread(2));
    }

    SECTION("terminate unknown thread") {
      map.terminateThread(100);
    }

    SECTION("create and terminate thread") {
      map.newThread(3);
      map.terminateThread(3);
    }

    SECTION("terminate thread that has had events") {
      map.erase(2, 2);
      map.terminateThread(2);
    }
  }
}

}  // namespace shk
