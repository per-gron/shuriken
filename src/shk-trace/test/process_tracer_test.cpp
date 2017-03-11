#include <catch.hpp>

#include <deque>
#include <utility>

#include "process_tracer.h"

namespace shk {
namespace {

class MockDelegate : public ProcessTracer::Delegate {
 public:
  MockDelegate(int &death_counter) : _death_counter(death_counter) {}

  ~MockDelegate() {
    _death_counter++;
    CHECK(_file_events.empty());
  }

  virtual void fileEvent(Tracer::EventType type, std::string &&path) override {
    _file_events.emplace_back(type, std::move(path));
  }

  std::pair<Tracer::EventType, std::string> popFileEvent() {
    REQUIRE(!_file_events.empty());
    auto result = _file_events.front();
    _file_events.pop_front();
    return result;
  }

 private:
  int &_death_counter;
  std::deque<std::pair<Tracer::EventType, std::string>> _file_events;
};

}  // anonymous namespace

TEST_CASE("ProcessTracer") {
  ProcessTracer tracer;

  int dead_tracers = 0;

  auto delegate_ptr = std::unique_ptr<MockDelegate>(
      new MockDelegate(dead_tracers));
  auto &delegate = *delegate_ptr;
  tracer.traceProcess(1, std::move(delegate_ptr));
  tracer.newThread(2, 3, /*pid:*/1);

  auto delegate2_ptr = std::unique_ptr<MockDelegate>(
      new MockDelegate(dead_tracers));
  auto &delegate2 = *delegate2_ptr;
  tracer.traceProcess(2, std::move(delegate2_ptr));

  SECTION("PidIsNotThreadId") {
    // should be dropped
    tracer.fileEvent(1, Tracer::EventType::FATAL_ERROR, "");
  }

  SECTION("EventForwarding") {
    SECTION("UnknownThreadId") {
      tracer.fileEvent(2, Tracer::EventType::FATAL_ERROR, "");
      tracer.fileEvent(123, Tracer::EventType::FATAL_ERROR, "");
    }

    SECTION("FileEvent") {
      tracer.fileEvent(3, Tracer::EventType::CREATE, "abc");
      auto evt = delegate.popFileEvent();
      CHECK(evt.first == Tracer::EventType::CREATE);
      CHECK(evt.second == "abc");
    }

    SECTION("MultipleDelegates") {
      tracer.newThread(4, 5, /*pid:*/2);
      tracer.fileEvent(5, Tracer::EventType::FATAL_ERROR, "");
      delegate2.popFileEvent();
    }
  }

  SECTION("DescendantFollowing") {
    SECTION("OneChild") {
      tracer.newThread(3, 4, /*pid:*/543);
      tracer.fileEvent(4, Tracer::EventType::FATAL_ERROR, "");
      delegate.popFileEvent();
    }

    SECTION("TwoGenerations") {
      tracer.newThread(3, 4, /*pid:*/543);
      tracer.newThread(4, 5, /*pid:*/543);
      tracer.fileEvent(5, Tracer::EventType::FATAL_ERROR, "");
      delegate.popFileEvent();
    }

    SECTION("TwoGenerationsIntermediaryDead") {
      tracer.newThread(3, 4, /*pid:*/543);
      tracer.newThread(4, 5, /*pid:*/543);
      tracer.terminateThread(4);
      tracer.fileEvent(5, Tracer::EventType::FATAL_ERROR, "");
      delegate.popFileEvent();
    }
  }

  SECTION("Termination") {
    SECTION("DontTraceThreadAfterItsTerminated") {
      tracer.newThread(3, 4, /*pid:*/543);
      tracer.terminateThread(4);
      tracer.fileEvent(4, Tracer::EventType::FATAL_ERROR, "");
    }

    SECTION("MainThreadTermination") {
      CHECK(dead_tracers == 0);
      tracer.terminateThread(3);
      CHECK(dead_tracers == 1);
    }
  }
}

}  // namespace shk
