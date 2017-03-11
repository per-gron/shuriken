#include <catch.hpp>

#include "event.h"

namespace shk {

TEST_CASE("Event") {
  SECTION("EventTypeToString") {
    CHECK(eventTypeToString(EventType::READ) == std::string("read"));
    CHECK(eventTypeToString(EventType::WRITE) == std::string("write"));
    CHECK(eventTypeToString(EventType::CREATE) == std::string("create"));
    CHECK(eventTypeToString(EventType::DELETE) == std::string("delete"));
    CHECK(eventTypeToString(EventType::FATAL_ERROR) == std::string("fatal_error"));
  }
}

}  // namespace shk
