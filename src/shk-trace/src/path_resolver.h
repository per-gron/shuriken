#pragma once

#include "cwd_memo.h"
#include "file_descriptor_memo.h"
#include "tracer.h"

namespace shk {

/**
 * PathResolver is a Tracer::Delegate that receives tracing events with
 * potentially relative paths and resolves them. It forwards the events, with
 * only absolute paths, to another Tracer::Delegate.
 */
class PathResolver : public Tracer::Delegate {
 public:
  /**
   * PathResolver::Delegate is a stripped down version of Tracer::Delegate. Its
   * purpose is to allow PathResolver to not copy strings to callbacks that
   * aren't going to be used anyway.
   */
  class Delegate {
   public:
    virtual ~Delegate() = default;

    virtual void fileEvent(
        pid_t pid,
        uintptr_t thread_id,
        EventType type,
        int at_fd,
        std::string &&path) = 0;
  };

  PathResolver(
      Delegate &delegate, pid_t initial_pid, std::string &&initial_cwd);

  virtual void newThread(
      pid_t pid,
      uintptr_t parent_thread_id,
      uintptr_t child_thread_id) override;

  virtual void terminateThread(uintptr_t thread_id) override;

  virtual void fileEvent(
    pid_t pid,
    uintptr_t thread_id,
    EventType type,
    int at_fd,
    std::string &&path) override;

  virtual void open(
      pid_t pid,
      uintptr_t thread_id,
      int fd,
      int at_fd,
      std::string &&path,
      bool cloexec) override;
  virtual void dup(
      pid_t pid,
      uintptr_t thread_id,
      int from_fd,
      int to_fd,
      bool cloexec) override;
  virtual void setCloexec(
      pid_t pid, uintptr_t thread_id, int fd, bool cloexec) override;
  virtual void fork(pid_t ppid, uintptr_t thread_id, pid_t pid) override;
  virtual void close(pid_t pid, uintptr_t thread_id, int fd) override;
  virtual void chdir(
      pid_t pid, uintptr_t thread_id, std::string &&path, int at_fd) override;
  virtual void threadChdir(
      pid_t pid, uintptr_t thread_id, std::string &&path, int at_fd) override;
  virtual void exec(pid_t pid, uintptr_t thread_id) override;

 private:
  std::string resolve(
      pid_t pid, uintptr_t thread_id, int at_fd, std::string &&path);

  Delegate &_delegate;
  CwdMemo _cwd_memo;
  FileDescriptorMemo _file_descriptor_memo;
};

}  // namespace shk
