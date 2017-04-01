#include <catch.hpp>

#include "event_consolidator.h"

namespace shk {
namespace {

bool containsFatalError(const std::vector<EventConsolidator::Event> &events) {
  return std::find_if(events.begin(), events.end(), [](
      const EventConsolidator::Event &event) {
    return event.first == EventType::FatalError;
  }) != events.end();
}

}  // anonymous namespace

TEST_CASE("EventConsolidator") {
  using ET = EventType;
  using SB = SymlinkBehavior;

  EventConsolidator ec;

  SECTION("Copyable") {
    ec.event(ET::FatalError, "", SB::NO_FOLLOW);
    auto ec2 = ec;

    CHECK(containsFatalError(ec.getConsolidatedEventsAndReset()));
    CHECK(containsFatalError(ec2.getConsolidatedEventsAndReset()));
  }

  SECTION("Assignable") {
    EventConsolidator ec2;
    ec2.event(ET::FatalError, "", SB::NO_FOLLOW);
    ec = ec2;

    CHECK(containsFatalError(ec.getConsolidatedEventsAndReset()));
    CHECK(containsFatalError(ec2.getConsolidatedEventsAndReset()));
  }

  SECTION("Reset") {
    static constexpr ET events[] = {
        ET::Read, ET::Write, ET::Create, ET::Delete, ET::FatalError };
    for (const auto event : events) {
      ec.event(event, "", SB::NO_FOLLOW);
      CHECK(!ec.getConsolidatedEventsAndReset().empty());
      CHECK(ec.getConsolidatedEventsAndReset().empty());
    }
  }

  SECTION("MergeDuplicateEvents") {
    static constexpr ET events[] = {
        ET::Read, ET::Write, ET::Create, ET::Delete, ET::FatalError };
    for (const auto event : events) {
      ec.event(event, "a", SB::NO_FOLLOW);
      ec.event(event, "a", SB::NO_FOLLOW);
      CHECK(ec.getConsolidatedEventsAndReset().size() == 1);
    }
  }

  SECTION("KeepPathAndEventType") {
    static constexpr ET events[] = {
        ET::Read, ET::Write, ET::Create, ET::Delete, ET::FatalError };
    for (const auto event : events) {
      ec.event(event, "path", SB::NO_FOLLOW);

      auto res = ec.getConsolidatedEventsAndReset();
      REQUIRE(res.size() == 1);
      CHECK(res.front().first == event);
      CHECK(res.front().second == "path");
    }
  }

  SECTION("IgnoreWriteAfterCreate") {
    ec.event(ET::Create, "path", SB::NO_FOLLOW);
    ec.event(ET::Write, "path", SB::NO_FOLLOW);

    auto res = ec.getConsolidatedEventsAndReset();
    REQUIRE(res.size() == 1);
    CHECK(res.front().first == ET::Create);
    CHECK(res.front().second == "path");
  }

  SECTION("CreateOverridesDelete") {
    ec.event(ET::Delete, "path", SB::NO_FOLLOW);
    ec.event(ET::Create, "path", SB::NO_FOLLOW);

    auto res = ec.getConsolidatedEventsAndReset();
    REQUIRE(res.size() == 1);
    CHECK(res.front().first == ET::Create);
    CHECK(res.front().second == "path");
  }

  SECTION("CreateAndDeleteOverrideWrite") {
    static constexpr ET events[] = { ET::Create, ET::Delete };

    for (auto event : events) {
      ec.event(ET::Write, "path", SB::NO_FOLLOW);
      ec.event(event, "path", SB::NO_FOLLOW);

      auto res = ec.getConsolidatedEventsAndReset();
      REQUIRE(res.size() == 1);
      CHECK(res.front().first == event);
      CHECK(res.front().second == "path");
    }
  }

  SECTION("CreateWriteDeleteDoNotOverrideRead") {
    static constexpr ET events[] = { ET::Create, ET::Write, ET::Delete };

    for (auto event : events) {
      ec.event(ET::Read, "path", SB::NO_FOLLOW);
      ec.event(event, "path", SB::NO_FOLLOW);

      auto res = ec.getConsolidatedEventsAndReset();
      REQUIRE(res.size() == 2);
      if (res[0].first != ET::Read) {
        // Crude sort
        swap(res[0], res[1]);
      }

      CHECK(res[0].first == ET::Read);
      CHECK(res[0].second == "path");
      CHECK(res[1].first == event);
      CHECK(res[1].second == "path");
    }
  }

  SECTION("DeleteErasesCreate") {
    ec.event(ET::Create, "path", SB::NO_FOLLOW);
    ec.event(ET::Delete, "path", SB::NO_FOLLOW);

    CHECK(ec.getConsolidatedEventsAndReset().empty());
  }
}

}  // namespace shk
