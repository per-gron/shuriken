#include "process_tracer.h"

namespace shk {

void ProcessTracer::traceProcess(
    int pid, std::unique_ptr<Delegate> &&delegate) {
  _to_be_traced.emplace(pid, std::move(delegate));
}

void ProcessTracer::newThread(
    uintptr_t parent_thread_id,
    uintptr_t child_thread_id,
    int parent_pid) {
  auto ancestor_id = findAncestor(parent_thread_id);
  if (_traced_threads.count(ancestor_id)) {
    // This is a child thread of an already traced thread.
    bool success = _ancestor_threads.emplace(
        child_thread_id, ancestor_id).second;
    if (!success) {
      throw std::runtime_error(
          "Created already existing thread. This should not happen.");
    }
    return;
  }

  auto to_be_traced_it = _to_be_traced.find(parent_pid);
  if (to_be_traced_it != _to_be_traced.end()) {
    // This is a thread that is enqueued to be traced
    _traced_threads.emplace(child_thread_id, std::move(to_be_traced_it->second));
    _to_be_traced.erase(to_be_traced_it);
    return;
  }
}

void ProcessTracer::terminateThread(uintptr_t thread_id) {
  auto ancestor_it = _ancestor_threads.find(thread_id);
  if (ancestor_it != _ancestor_threads.end()) {
    // This thread is a child thread of a traced thread. Forget it.
    _ancestor_threads.erase(ancestor_it);
    return;
  }

  auto delegate_it = _traced_threads.find(thread_id);
  if (delegate_it != _traced_threads.end()) {
    // This thread is an ancestor traced thread. Finish the tracing by
    // destroying the delegate.
    _traced_threads.erase(delegate_it);
    return;
  }
}

void ProcessTracer::illegalEvent(uintptr_t thread_id) {
  if (auto delegate = findDelegate(thread_id)) {
    delegate->illegalEvent();
  }
}

void ProcessTracer::fileEvent(
    uintptr_t thread_id, Tracer::EventType type, std::string &&path) {
  if (auto delegate = findDelegate(thread_id)) {
    delegate->fileEvent(type, std::move(path));
  }
}

ProcessTracer::Delegate *ProcessTracer::findDelegate(uintptr_t thread_id) {
  auto delegate_it = _traced_threads.find(findAncestor(thread_id));
  return delegate_it == _traced_threads.end() ?
      nullptr : delegate_it->second.get();
}

uintptr_t ProcessTracer::findAncestor(uintptr_t thread_id) {
  auto ancestor_it = _ancestor_threads.find(thread_id);
  return ancestor_it == _ancestor_threads.end() ?
      thread_id : ancestor_it->second;
}


}  // namespace shk
