#include <catch.hpp>

#include "event_consolidator.h"

namespace shk {
namespace {

bool containsFatalError(const std::vector<EventConsolidator::Event> &events) {
  return std::find_if(events.begin(), events.end(), [](
      const EventConsolidator::Event &event) {
    return event.first == Tracer::EventType::FATAL_ERROR;
  }) != events.end();
}

}  // anonymous namespace

TEST_CASE("EventConsolidator") {
  using ET = Tracer::EventType;

  EventConsolidator ec;

  SECTION("Copyable") {
    ec.event(ET::FATAL_ERROR, "");
    auto ec2 = ec;

    CHECK(containsFatalError(ec.getConsolidatedEventsAndReset()));
    CHECK(containsFatalError(ec2.getConsolidatedEventsAndReset()));
  }

  SECTION("Assignable") {
    EventConsolidator ec2;
    ec2.event(ET::FATAL_ERROR, "");
    ec = ec2;

    CHECK(containsFatalError(ec.getConsolidatedEventsAndReset()));
    CHECK(containsFatalError(ec2.getConsolidatedEventsAndReset()));
  }

  SECTION("Reset") {
    static constexpr ET events[] = {
        ET::READ, ET::WRITE, ET::CREATE, ET::DELETE, ET::FATAL_ERROR };
    for (const auto event : events) {
      ec.event(event, "");
      CHECK(!ec.getConsolidatedEventsAndReset().empty());
      CHECK(ec.getConsolidatedEventsAndReset().empty());
    }
  }

  SECTION("MergeDuplicateEvents") {
    static constexpr ET events[] = {
        ET::READ, ET::WRITE, ET::CREATE, ET::DELETE, ET::FATAL_ERROR };
    for (const auto event : events) {
      ec.event(event, "a");
      ec.event(event, "a");
      CHECK(ec.getConsolidatedEventsAndReset().size() == 1);
    }
  }

  SECTION("KeepPathAndEventType") {
    static constexpr ET events[] = {
        ET::READ, ET::WRITE, ET::CREATE, ET::DELETE };
    for (const auto event : events) {
      ec.event(event, "path");

      auto res = ec.getConsolidatedEventsAndReset();
      REQUIRE(res.size() == 1);
      CHECK(res.front().first == event);
      CHECK(res.front().second == "path");
    }
  }

  SECTION("IgnoreWriteAfterCreate") {
    ec.event(ET::CREATE, "path");
    ec.event(ET::WRITE, "path");

    auto res = ec.getConsolidatedEventsAndReset();
    REQUIRE(res.size() == 1);
    CHECK(res.front().first == ET::CREATE);
    CHECK(res.front().second == "path");
  }

  SECTION("CreateOverridesDelete") {
    ec.event(ET::DELETE, "path");
    ec.event(ET::CREATE, "path");

    auto res = ec.getConsolidatedEventsAndReset();
    REQUIRE(res.size() == 1);
    CHECK(res.front().first == ET::CREATE);
    CHECK(res.front().second == "path");
  }

  SECTION("CreateAndDeleteOverrideWrite") {
    static constexpr ET events[] = { ET::CREATE, ET::DELETE };

    for (auto event : events) {
      ec.event(ET::WRITE, "path");
      ec.event(event, "path");

      auto res = ec.getConsolidatedEventsAndReset();
      REQUIRE(res.size() == 1);
      CHECK(res.front().first == event);
      CHECK(res.front().second == "path");
    }
  }

  SECTION("CreateWriteDeleteDoNotOverrideRead") {
    static constexpr ET events[] = { ET::CREATE, ET::WRITE, ET::DELETE };

    for (auto event : events) {
      ec.event(ET::READ, "path");
      ec.event(event, "path");

      auto res = ec.getConsolidatedEventsAndReset();
      REQUIRE(res.size() == 2);
      if (res[0].first != ET::READ) {
        // Crude sort
        swap(res[0], res[1]);
      }

      CHECK(res[0].first == ET::READ);
      CHECK(res[0].second == "path");
      CHECK(res[1].first == event);
      CHECK(res[1].second == "path");
    }
  }

  SECTION("DeleteErasesCreate") {
    ec.event(ET::CREATE, "path");
    ec.event(ET::DELETE, "path");

    CHECK(ec.getConsolidatedEventsAndReset().empty());
  }
}

}  // namespace shk
