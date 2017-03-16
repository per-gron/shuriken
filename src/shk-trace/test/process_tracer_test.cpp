#include <catch.hpp>

#include <deque>
#include <fcntl.h>
#include <utility>

#include "process_tracer.h"

#include "mock_tracer_delegate.h"

namespace shk {

TEST_CASE("ProcessTracer") {
  ProcessTracer tracer;

  int dead_tracers = 0;

  auto delegate_ptr = std::unique_ptr<MockTracerDelegate>(
      new MockTracerDelegate(dead_tracers));
  auto &delegate = *delegate_ptr;
  tracer.traceProcess(1, std::move(delegate_ptr));
  tracer.newThread(/*pid:*/1, 2, 3);
  delegate.popNewThreadEvent();

  auto delegate2_ptr = std::unique_ptr<MockTracerDelegate>(
      new MockTracerDelegate(dead_tracers));
  auto &delegate2 = *delegate2_ptr;
  tracer.traceProcess(2, std::move(delegate2_ptr));

  SECTION("PidIsNotThreadId") {
    // should be dropped
    tracer.fileEvent(0, 1, EventType::FATAL_ERROR, AT_FDCWD, "");
  }

  SECTION("EventForwarding") {
    SECTION("UnknownThreadId") {
      tracer.fileEvent(1, 2, EventType::FATAL_ERROR, AT_FDCWD, "");
      tracer.fileEvent(1, 123, EventType::FATAL_ERROR, AT_FDCWD, "");
    }

    SECTION("FileEvent") {
      tracer.fileEvent(1, 3, EventType::CREATE, 999, "abc");
      auto evt = delegate.popFileEvent();
      CHECK(evt.pid == 1);
      CHECK(evt.thread_id == 3);
      CHECK(evt.type == EventType::CREATE);
      CHECK(evt.at_fd == 999);
      CHECK(evt.path == "abc");
    }

    SECTION("TerminateThreadEventForAncestor") {
      delegate.expectTermination();
      tracer.terminateThread(3);
    }

    SECTION("TerminateThreadEventForChildThread") {
      tracer.newThread(/*pid:*/1, 3, 4);
      auto event = delegate.popNewThreadEvent();
      tracer.terminateThread(4);
      CHECK(delegate.popTerminateThreadEvent() == 4);
    }

    SECTION("NewThreadForNewTrace") {
      tracer.newThread(/*pid:*/2, 4, 5);
      auto event = delegate2.popNewThreadEvent();
      CHECK(event.parent_thread_id == 4);
      CHECK(event.child_thread_id == 5);
      CHECK(event.pid == 2);
    }

    SECTION("NewThreadForCurrentTrace") {
      tracer.newThread(/*pid:*/1, 3, 4);
      auto event = delegate.popNewThreadEvent();
      CHECK(event.parent_thread_id == 3);
      CHECK(event.child_thread_id == 4);
      CHECK(event.pid == 1);
    }

    SECTION("MultipleDelegates") {
      tracer.newThread(/*pid:*/2, 4, 5);
      delegate2.popNewThreadEvent();
      tracer.fileEvent(1, 5, EventType::FATAL_ERROR, AT_FDCWD, "");
      delegate2.popFileEvent();
    }

    SECTION("OpenEvent") {
      tracer.open(12, 3, 13, 14, "hey", false);
      auto event = delegate.popOpenEvent();
      CHECK(event.thread_id == 3);
      CHECK(event.pid == 12);
      CHECK(event.fd == 13);
      CHECK(event.at_fd == 14);
      CHECK(event.path == "hey");
      CHECK(event.cloexec == false);
    }

    SECTION("OpenEventUnknownThreadId") {
      tracer.open(11, 12, 13, 14, "hey", true);
    }

    SECTION("DupEvent") {
      tracer.dup(12, 3, 13, 14);
      auto event = delegate.popDupEvent();
      CHECK(event.thread_id == 3);
      CHECK(event.pid == 12);
      CHECK(event.from_fd == 13);
      CHECK(event.to_fd == 14);
    }

    SECTION("DupEventUnknownThreadId") {
      tracer.dup(11, 12, 13, 14);
    }

    SECTION("SetCloexecEvent") {
      tracer.setCloexec(12, 3, 13, false);
      auto event = delegate.popSetCloexecEvent();
      CHECK(event.thread_id == 3);
      CHECK(event.pid == 12);
      CHECK(event.fd == 13);
      CHECK(event.cloexec == false);
    }

    SECTION("SetCloexecEventUnknownThreadId") {
      tracer.setCloexec(11, 12, 13, true);
    }

    SECTION("ForkEvent") {
      tracer.fork(12, 3, 13);
      auto event = delegate.popForkEvent();
      CHECK(event.thread_id == 3);
      CHECK(event.ppid == 12);
      CHECK(event.pid == 13);
    }

    SECTION("ForkEventUnknownThreadId") {
      tracer.fork(11, 12, 13);
    }

    SECTION("CloseEvent") {
      tracer.close(12, 3, 13);
      auto event = delegate.popCloseEvent();
      CHECK(event.thread_id == 3);
      CHECK(event.pid == 12);
      CHECK(event.fd == 13);
    }

    SECTION("CloseEventUnknownThreadId") {
      tracer.close(11, 12, 13);
    }

    SECTION("ChdirEvent") {
      tracer.chdir(12, 3, "hey", 13);
      auto event = delegate.popChdirEvent();
      CHECK(event.thread_id == 3);
      CHECK(event.pid == 12);
      CHECK(event.path == "hey");
      CHECK(event.at_fd == 13);
    }

    SECTION("ChdirEventUnknownThreadId") {
      tracer.chdir(11, 12, "hey", 13);
    }

    SECTION("ThreadChdirEvent") {
      tracer.threadChdir(1, 3, "lol", 12);
      auto event = delegate.popThreadChdirEvent();
      CHECK(event.pid == 1);
      CHECK(event.thread_id == 3);
      CHECK(event.path == "lol");
      CHECK(event.at_fd == 12);
    }

    SECTION("ThreadChdirEventUnknownThreadId") {
      tracer.threadChdir(10, 11, "lol", 12);
    }

    SECTION("ExecEvent") {
      tracer.exec(12, 3);
      auto event = delegate.popExecEvent();
      CHECK(event.thread_id == 3);
      CHECK(event.pid == 12);
    }

    SECTION("ExecEventUnknownThreadId") {
      tracer.exec(11, 12);
    }

  }

  SECTION("DescendantFollowing") {
    SECTION("OneChild") {
      tracer.newThread(/*pid:*/543, 3, 4);
      delegate.popNewThreadEvent();
      tracer.fileEvent(1, 4, EventType::FATAL_ERROR, AT_FDCWD, "");
      delegate.popFileEvent();
    }

    SECTION("TwoGenerations") {
      tracer.newThread(/*pid:*/543, 3, 4);
      delegate.popNewThreadEvent();
      tracer.newThread(/*pid:*/543, 4, 5);
      delegate.popNewThreadEvent();
      tracer.fileEvent(1, 5, EventType::FATAL_ERROR, AT_FDCWD, "");
      delegate.popFileEvent();
    }

    SECTION("TwoGenerationsIntermediaryDead") {
      tracer.newThread(/*pid:*/543, 3, 4);
      delegate.popNewThreadEvent();
      tracer.newThread(/*pid:*/543, 4, 5);
      delegate.popNewThreadEvent();
      tracer.terminateThread(4);
      delegate.popTerminateThreadEvent();
      tracer.fileEvent(1, 5, EventType::FATAL_ERROR, AT_FDCWD, "");
      delegate.popFileEvent();
    }
  }

  SECTION("Termination") {
    SECTION("DontTraceThreadAfterItsTerminated") {
      tracer.newThread(/*pid:*/543, 3, 4);
      delegate.popNewThreadEvent();
      tracer.terminateThread(4);
      delegate.popTerminateThreadEvent();
      tracer.fileEvent(1, 4, EventType::FATAL_ERROR, AT_FDCWD, "");
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
