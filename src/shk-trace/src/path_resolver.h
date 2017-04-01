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
    using SymlinkBehavior = Tracer::Delegate::SymlinkBehavior;

    virtual ~Delegate() = default;

    virtual void fileEvent(
        EventType type,
        std::string &&path,
        SymlinkBehavior symlink_behavior) = 0;
  };

  PathResolver(
      std::unique_ptr<Delegate> &&delegate,
      pid_t initial_pid,
      std::string &&initial_cwd);

  virtual void newThread(
      pid_t pid,
      uintptr_t parent_thread_id,
      uintptr_t child_thread_id) override;

  virtual Response terminateThread(uintptr_t thread_id) override;

  virtual void fileEvent(
    uintptr_t thread_id,
    EventType type,
    int at_fd,
    std::string &&path,
    SymlinkBehavior symlink_behavior) override;

  virtual void open(
      uintptr_t thread_id,
      int fd,
      int at_fd,
      std::string &&path,
      bool cloexec) override;
  virtual void dup(
      uintptr_t thread_id,
      int from_fd,
      int to_fd,
      bool cloexec) override;
  virtual void setCloexec(
      uintptr_t thread_id, int fd, bool cloexec) override;
  virtual void close(uintptr_t thread_id, int fd) override;
  virtual void chdir(
      uintptr_t thread_id, std::string &&path, int at_fd) override;
  virtual void threadChdir(
      uintptr_t thread_id, std::string &&path, int at_fd) override;
  virtual void exec(uintptr_t thread_id) override;

 private:
  std::string resolve(
      uintptr_t thread_id, int at_fd, std::string &&path);

  const pid_t *getPid(uintptr_t thread_id) const;

  std::unique_ptr<Delegate> _delegate;
  std::unordered_map<uintptr_t, pid_t> _pids;
  CwdMemo _cwd_memo;
  FileDescriptorMemo _file_descriptor_memo;
};

}  // namespace shk
