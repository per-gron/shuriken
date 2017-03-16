#pragma once

namespace shk {

struct FileEvent {
  pid_t pid;
  uintptr_t thread_id;
  EventType type;
  int at_fd;
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
  bool cloexec;
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
  pid_t pid;
  uintptr_t thread_id;
  std::string path;
  int at_fd;
};

struct ExecEvent {
  pid_t pid;
  uintptr_t thread_id;
};

class MockTracerDelegate : public Tracer::Delegate {
 public:
  MockTracerDelegate(int &death_counter) : _death_counter(death_counter) {}

  ~MockTracerDelegate() {
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
      pid_t pid,
      uintptr_t thread_id,
      EventType type,
      int at_fd,
      std::string &&path) override {
    _file_events.push_back(
        FileEvent{ pid, thread_id, type, at_fd, std::move(path) });
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
      pid_t pid,
      uintptr_t thread_id,
      int from_fd,
      int to_fd,
      bool cloexec) override {
    _dup_events.push_back(DupEvent{
        pid, thread_id, from_fd, to_fd, cloexec });
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
      pid_t pid, uintptr_t thread_id, std::string &&path, int at_fd) override {
    _thread_chdir_events.push_back(ThreadChdirEvent{
        pid, thread_id, std::move(path), at_fd });
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

}  // namespace shk
