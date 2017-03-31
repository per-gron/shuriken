#include <catch.hpp>

#include "event.h"

namespace shk {

TEST_CASE("Event") {
  SECTION("EventTypeToString") {
    CHECK(eventTypeToString(EventType::Read) == std::string("read"));
    CHECK(eventTypeToString(EventType::Write) == std::string("write"));
    CHECK(eventTypeToString(EventType::Create) == std::string("create"));
    CHECK(eventTypeToString(EventType::Delete) == std::string("delete"));
    CHECK(eventTypeToString(EventType::FatalError) == std::string("fatal_error"));
  }
}

}  // namespace shk
