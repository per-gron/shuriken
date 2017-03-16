#pragma once

#include <unordered_map>

#include "event.h"
#include "tracer.h"

namespace shk {

/**
 * ProcessTracer is one level above the Tracer class. It is responsible for
 * organizing events from a global stream into one stream per process and its
 * children. It receives events from a Tracer (via the Tracer::Delegate
 * interface) and emits per-process (including child processes) events to
 * Tracer::Delegate objects (one per trace).
 */
class ProcessTracer : public Tracer::Delegate {
 public:
  ProcessTracer() = default;
  ProcessTracer(const ProcessTracer &) = delete;
  ProcessTracer &operator=(const ProcessTracer &) = delete;

  /**
   * ProcessTracer assumes ownership of the Delegate object that is given to the
   * traceProcess method. The Delegate object is destroyed when the traced
   * process has terminated.
   */
  void traceProcess(pid_t pid, std::unique_ptr<Tracer::Delegate> &&delegate);

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
  struct Ancestor {
    Ancestor(uintptr_t ancestor_thread_id, Tracer::Delegate *delegate)
        : ancestor_thread_id(ancestor_thread_id),
          delegate(delegate) {}

    uintptr_t ancestor_thread_id = 0;
    Tracer::Delegate *delegate = nullptr;

    explicit operator bool() const {
      return !!delegate;
    }
  };

  // Returns Ancestor without delegate if the thread is not being traced.
  // Returns Ancestor with ancestor_thread_id == thread_id if thread_id is an
  // ancestor itself.
  Ancestor findAncestor(uintptr_t thread_id);

  // Map pid => Delegate, for processes where we don't yet know the thread id.
  using ToBeTracedQueue =
      std::unordered_map<pid_t, std::unique_ptr<Tracer::Delegate>>;

  // Map traced child thread id => Tracer::Delegate (not owning ref). For each
  // traced process, there is also an entry for the ancestor thread in this map.
  using AncestorThreads = std::unordered_map<uintptr_t, Ancestor>;

  // Map traced ancestor thread id => Delegate for tracing
  using TracedThreads =
      std::unordered_map<uintptr_t, std::unique_ptr<Tracer::Delegate>>;

  ToBeTracedQueue _to_be_traced;
  AncestorThreads _ancestor_threads;
  TracedThreads _traced_threads;
};

}  // namespace shk
