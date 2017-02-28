#pragma once

#include <unordered_map>

#include "tracer.h"

namespace shk {

/**
 * ProcessTracer is one level above the Tracer class. It is responsible for
 * organizing events from a global stream into one stream per process and its
 * children. It receives events from a Tracer (via the Tracer::Delegate
 * interface) and emits per-process (including child processes) events to
 * ProcessTracer::Delegate objects.
 */
class ProcessTracer : public Tracer::Delegate {
 public:
  class Delegate {
   public:
    /**
     * Delegate is destroyed when the traced process has terminated.
     */
    virtual ~Delegate() = default;

    /**
     * Encountered an event that the ProcessTracer does not understand. For
     * threads where this happens, the tracer report may miss things. This
     * happens for legacy Carbon File Manager system calls.
     */
    virtual void illegalEvent() = 0;

    virtual void fileEvent(Tracer::EventType type, std::string &&path) = 0;
  };

  ProcessTracer() = default;
  ProcessTracer(const ProcessTracer &) = delete;
  ProcessTracer &operator=(const ProcessTracer &) = delete;

  /**
   * ProcessTracer assumes ownership of the Delegate object that is given to the
   * traceProcess method. The Delegate object is destroyed when the traced
   * process has terminated.
   */
  void traceProcess(int pid, std::unique_ptr<Delegate> &&delegate);

  virtual void newThread(
      uintptr_t parent_thread_id,
      uintptr_t child_thread_id,
      int parent_pid) override;

  virtual void terminateThread(uintptr_t thread_id) override;

  virtual void illegalEvent(uintptr_t thread_id) override;

  virtual void fileEvent(
      uintptr_t thread_id, Tracer::EventType type, std::string &&path) override;

 private:
  // Returns nullptr on not found
  Delegate *findDelegate(uintptr_t thread_id);
  // Returns thread_id if there is no known ancestor
  uintptr_t findAncestor(uintptr_t thread_id);

  // Map pid => Delegate, for processes where we don't yet know the thread id.
  using ToBeTracedQueue = std::unordered_map<int, std::unique_ptr<Delegate>>;

  // Map traced child thread id => traced ancestor thread id. There are no
  // entries for ancestor threads themselves in this map.
  using AncestorThreadIds = std::unordered_map<uintptr_t, uintptr_t>;

  // Map traced ancestor thread id => Delegate for tracing
  using TracedThreads = std::unordered_map<uintptr_t, std::unique_ptr<Delegate>>;

  ToBeTracedQueue _to_be_traced;
  AncestorThreadIds _ancestor_threads;
  TracedThreads _traced_threads;
};

}  // namespace shk
