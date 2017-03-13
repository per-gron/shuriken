#include <catch.hpp>

#include <deque>
#include <utility>

#include "process_tracer.h"

namespace shk {
namespace {

struct FileEvent {
  uintptr_t thread_id;
  EventType type;
  std::string path;
};

struct NewThreadEvent {
  uintptr_t parent_thread_id;
  uintptr_t child_thread_id;
  pid_t pid;
};

class MockDelegate : public Tracer::Delegate {
 public:
  MockDelegate(int &death_counter) : _death_counter(death_counter) {}

  ~MockDelegate() {
    _death_counter++;
    CHECK(_file_events.empty());
    CHECK(_new_thread_events.empty());
    // The test fixture will cannot pop the terminate thread event for the
    // ancestor thread before this object dies. To avoid this problem, we allow
    // tests to claim that the thread will be terminated in advance instead.
    CHECK(_terminate_thread_events.size() == _expect_termination);
  }

  virtual void fileEvent(
      uintptr_t thread_id, EventType type, std::string &&path) override {
    _file_events.push_back(FileEvent{ thread_id, type, std::move(path) });
  }

  virtual void newThread(
      uintptr_t parent_thread_id,
      uintptr_t child_thread_id,
      pid_t pid) override {
    _new_thread_events.push_back(NewThreadEvent{
        parent_thread_id, child_thread_id, pid });
  }

  virtual void terminateThread(uintptr_t thread_id) override {
    _terminate_thread_events.push_back(thread_id);
  }

  FileEvent popFileEvent() {
    REQUIRE(!_file_events.empty());
    auto result = _file_events.front();
    _file_events.pop_front();
    return result;
  }

  NewThreadEvent popNewThreadEvent() {
    REQUIRE(!_new_thread_events.empty());
    auto result = _new_thread_events.front();
    _new_thread_events.pop_front();
    return result;
  }

  uintptr_t popTerminateThreadEvent() {
    REQUIRE(!_terminate_thread_events.empty());
    auto result = _terminate_thread_events.front();
    _terminate_thread_events.pop_front();
    return result;
  }

  void expectTermination() {
    _expect_termination = 1;
  }

 private:
  int _expect_termination = 0;
  int &_death_counter;
  std::deque<FileEvent> _file_events;
  std::deque<NewThreadEvent> _new_thread_events;
  std::deque<uint64_t> _terminate_thread_events;
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
  delegate.popNewThreadEvent();

  auto delegate2_ptr = std::unique_ptr<MockDelegate>(
      new MockDelegate(dead_tracers));
  auto &delegate2 = *delegate2_ptr;
  tracer.traceProcess(2, std::move(delegate2_ptr));

  SECTION("PidIsNotThreadId") {
    // should be dropped
    tracer.fileEvent(1, EventType::FATAL_ERROR, "");
  }

  SECTION("EventForwarding") {
    SECTION("UnknownThreadId") {
      tracer.fileEvent(2, EventType::FATAL_ERROR, "");
      tracer.fileEvent(123, EventType::FATAL_ERROR, "");
    }

    SECTION("FileEvent") {
      tracer.fileEvent(3, EventType::CREATE, "abc");
      auto evt = delegate.popFileEvent();
      CHECK(evt.thread_id == 3);
      CHECK(evt.type == EventType::CREATE);
      CHECK(evt.path == "abc");
    }

    SECTION("TerminateThreadEventForAncestor") {
      delegate.expectTermination();
      tracer.terminateThread(3);
    }

    SECTION("TerminateThreadEventForChildThread") {
      tracer.newThread(3, 4, /*pid:*/1);
      auto event = delegate.popNewThreadEvent();
      tracer.terminateThread(4);
      CHECK(delegate.popTerminateThreadEvent() == 4);
    }

    SECTION("NewThreadForNewTrace") {
      tracer.newThread(4, 5, /*pid:*/2);
      auto event = delegate2.popNewThreadEvent();
      CHECK(event.parent_thread_id == 4);
      CHECK(event.child_thread_id == 5);
      CHECK(event.pid == 2);
    }

    SECTION("NewThreadForCurrentTrace") {
      tracer.newThread(3, 4, /*pid:*/1);
      auto event = delegate.popNewThreadEvent();
      CHECK(event.parent_thread_id == 3);
      CHECK(event.child_thread_id == 4);
      CHECK(event.pid == 1);
    }

    SECTION("MultipleDelegates") {
      tracer.newThread(4, 5, /*pid:*/2);
      delegate2.popNewThreadEvent();
      tracer.fileEvent(5, EventType::FATAL_ERROR, "");
      delegate2.popFileEvent();
    }
  }

  SECTION("DescendantFollowing") {
    SECTION("OneChild") {
      tracer.newThread(3, 4, /*pid:*/543);
      delegate.popNewThreadEvent();
      tracer.fileEvent(4, EventType::FATAL_ERROR, "");
      delegate.popFileEvent();
    }

    SECTION("TwoGenerations") {
      tracer.newThread(3, 4, /*pid:*/543);
      delegate.popNewThreadEvent();
      tracer.newThread(4, 5, /*pid:*/543);
      delegate.popNewThreadEvent();
      tracer.fileEvent(5, EventType::FATAL_ERROR, "");
      delegate.popFileEvent();
    }

    SECTION("TwoGenerationsIntermediaryDead") {
      tracer.newThread(3, 4, /*pid:*/543);
      delegate.popNewThreadEvent();
      tracer.newThread(4, 5, /*pid:*/543);
      delegate.popNewThreadEvent();
      tracer.terminateThread(4);
      delegate.popTerminateThreadEvent();
      tracer.fileEvent(5, EventType::FATAL_ERROR, "");
      delegate.popFileEvent();
    }
  }

  SECTION("Termination") {
    SECTION("DontTraceThreadAfterItsTerminated") {
      tracer.newThread(3, 4, /*pid:*/543);
      delegate.popNewThreadEvent();
      tracer.terminateThread(4);
      delegate.popTerminateThreadEvent();
      tracer.fileEvent(4, EventType::FATAL_ERROR, "");
    }

    SECTION("MainThreadTermination") {
      CHECK(dead_tracers == 0);
      delegate.expectTermination();
      tracer.terminateThread(3);
      CHECK(dead_tracers == 1);
    }
  }
}

}  // namespace shk
