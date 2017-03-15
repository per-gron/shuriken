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
  pid_t pid;
  uintptr_t parent_thread_id;
  uintptr_t child_thread_id;
};

struct OpenEvent {
  pid_t pid;
  uintptr_t thread_id;
  int fd;
  int at_fd;
  std::string path;
  bool cloexec;
};

struct DupEvent {
  pid_t pid;
  uintptr_t thread_id;
  int from_fd;
  int to_fd;
};

struct SetCloexecEvent {
  pid_t pid;
  uintptr_t thread_id;
  int fd;
  bool cloexec;
};

struct ForkEvent {
  pid_t ppid;
  uintptr_t thread_id;
  pid_t pid;
};

struct CloseEvent {
  pid_t pid;
  uintptr_t thread_id;
  int fd;
};

struct ChdirEvent {
  pid_t pid;
  uintptr_t thread_id;
  std::string path;
  int at_fd;
};

struct ThreadChdirEvent {
  uintptr_t thread_id;
  std::string path;
  int at_fd;
};

struct ExecEvent {
  pid_t pid;
  uintptr_t thread_id;
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

    CHECK(_open_events.empty());
    CHECK(_dup_events.empty());
    CHECK(_set_cloexec_events.empty());
    CHECK(_fork_events.empty());
    CHECK(_close_events.empty());
    CHECK(_chdir_events.empty());
    CHECK(_thread_chdir_events.empty());
    CHECK(_exec_events.empty());
  }

  virtual void fileEvent(
      uintptr_t thread_id, EventType type, std::string &&path) override {
    _file_events.push_back(FileEvent{ thread_id, type, std::move(path) });
  }

  virtual void newThread(
      pid_t pid,
      uintptr_t parent_thread_id,
      uintptr_t child_thread_id) override {
    _new_thread_events.push_back(NewThreadEvent{
        pid, parent_thread_id, child_thread_id });
  }

  virtual void terminateThread(uintptr_t thread_id) override {
    _terminate_thread_events.push_back(thread_id);
  }

  virtual void open(
      pid_t pid,
      uintptr_t thread_id,
      int fd,
      int at_fd,
      std::string &&path,
      bool cloexec) override {
    _open_events.push_back(OpenEvent{
        pid, thread_id, fd, at_fd, std::move(path), cloexec });
  }

  virtual void dup(
      pid_t pid, uintptr_t thread_id, int from_fd, int to_fd) override {
    _dup_events.push_back(DupEvent{
        pid, thread_id, from_fd, to_fd });
  }

  virtual void setCloexec(
      pid_t pid, uintptr_t thread_id, int fd, bool cloexec) override {
    _set_cloexec_events.push_back(SetCloexecEvent{
        pid, thread_id, fd, cloexec });
  }

  virtual void fork(pid_t ppid, uintptr_t thread_id, pid_t pid) override {
    _fork_events.push_back(ForkEvent{
        ppid, thread_id, pid });
  }

  virtual void close(pid_t pid, uintptr_t thread_id, int fd) override {
    _close_events.push_back(CloseEvent{
        pid, thread_id, fd });
  }

  virtual void chdir(
      pid_t pid, uintptr_t thread_id, std::string &&path, int at_fd) override {
    _chdir_events.push_back(ChdirEvent{
        pid, thread_id, std::move(path), at_fd });
  }

  virtual void threadChdir(
      uintptr_t thread_id, std::string &&path, int at_fd) override {
    _thread_chdir_events.push_back(ThreadChdirEvent{
        thread_id, std::move(path), at_fd });
  }

  virtual void exec(pid_t pid, uintptr_t thread_id) override {
    _exec_events.push_back(ExecEvent{
        pid, thread_id });
  }

  FileEvent popFileEvent() {
    return popFrontAndReturn(_file_events);
  }

  NewThreadEvent popNewThreadEvent() {
    return popFrontAndReturn(_new_thread_events);
  }

  uintptr_t popTerminateThreadEvent() {
    return popFrontAndReturn(_terminate_thread_events);
  }

  OpenEvent popOpenEvent() {
    return popFrontAndReturn(_open_events);
  }

  DupEvent popDupEvent() {
    return popFrontAndReturn(_dup_events);
  }

  SetCloexecEvent popSetCloexecEvent() {
    return popFrontAndReturn(_set_cloexec_events);
  }

  ForkEvent popForkEvent() {
    return popFrontAndReturn(_fork_events);
  }

  CloseEvent popCloseEvent() {
    return popFrontAndReturn(_close_events);
  }

  ChdirEvent popChdirEvent() {
    return popFrontAndReturn(_chdir_events);
  }

  ThreadChdirEvent popThreadChdirEvent() {
    return popFrontAndReturn(_thread_chdir_events);
  }

  ExecEvent popExecEvent() {
    return popFrontAndReturn(_exec_events);
  }


  void expectTermination() {
    _expect_termination = 1;
  }

 private:
  template <typename Container>
  typename Container::value_type popFrontAndReturn(Container &container) {
    REQUIRE(!container.empty());
    auto result = container.front();
    container.pop_front();
    return result;
  }

  int _expect_termination = 0;
  int &_death_counter;
  std::deque<FileEvent> _file_events;
  std::deque<NewThreadEvent> _new_thread_events;
  std::deque<uint64_t> _terminate_thread_events;
  std::deque<OpenEvent> _open_events;
  std::deque<DupEvent> _dup_events;
  std::deque<SetCloexecEvent> _set_cloexec_events;
  std::deque<ForkEvent> _fork_events;
  std::deque<CloseEvent> _close_events;
  std::deque<ChdirEvent> _chdir_events;
  std::deque<ThreadChdirEvent> _thread_chdir_events;
  std::deque<ExecEvent> _exec_events;
};

}  // anonymous namespace

TEST_CASE("ProcessTracer") {
  ProcessTracer tracer;

  int dead_tracers = 0;

  auto delegate_ptr = std::unique_ptr<MockDelegate>(
      new MockDelegate(dead_tracers));
  auto &delegate = *delegate_ptr;
  tracer.traceProcess(1, std::move(delegate_ptr));
  tracer.newThread(/*pid:*/1, 2, 3);
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
      tracer.fileEvent(5, EventType::FATAL_ERROR, "");
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
      tracer.threadChdir(3, "lol", 12);
      auto event = delegate.popThreadChdirEvent();
      CHECK(event.thread_id == 3);
      CHECK(event.path == "lol");
      CHECK(event.at_fd == 12);
    }

    SECTION("ThreadChdirEventUnknownThreadId") {
      tracer.threadChdir(11, "lol", 12);
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
      tracer.fileEvent(4, EventType::FATAL_ERROR, "");
      delegate.popFileEvent();
    }

    SECTION("TwoGenerations") {
      tracer.newThread(/*pid:*/543, 3, 4);
      delegate.popNewThreadEvent();
      tracer.newThread(/*pid:*/543, 4, 5);
      delegate.popNewThreadEvent();
      tracer.fileEvent(5, EventType::FATAL_ERROR, "");
      delegate.popFileEvent();
    }

    SECTION("TwoGenerationsIntermediaryDead") {
      tracer.newThread(/*pid:*/543, 3, 4);
      delegate.popNewThreadEvent();
      tracer.newThread(/*pid:*/543, 4, 5);
      delegate.popNewThreadEvent();
      tracer.terminateThread(4);
      delegate.popTerminateThreadEvent();
      tracer.fileEvent(5, EventType::FATAL_ERROR, "");
      delegate.popFileEvent();
    }
  }

  SECTION("Termination") {
    SECTION("DontTraceThreadAfterItsTerminated") {
      tracer.newThread(/*pid:*/543, 3, 4);
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
