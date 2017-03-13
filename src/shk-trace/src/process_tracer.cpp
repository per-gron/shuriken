#include "process_tracer.h"

namespace shk {

void ProcessTracer::traceProcess(
    pid_t pid, std::unique_ptr<Tracer::Delegate> &&delegate) {
  _to_be_traced.emplace(pid, std::move(delegate));
}

void ProcessTracer::newThread(
    uintptr_t parent_thread_id,
    uintptr_t child_thread_id,
    pid_t pid) {
  if (Ancestor ancestor = findAncestor(parent_thread_id)) {
    // This is a child thread of an already traced thread.
    bool success = _ancestor_threads.emplace(child_thread_id, ancestor).second;
    if (!success) {
      throw std::runtime_error(
          "Created already existing thread. This should not happen.");
    }
    ancestor.delegate->newThread(parent_thread_id, child_thread_id, pid);
    return;
  }

  auto to_be_traced_it = _to_be_traced.find(pid);
  if (to_be_traced_it != _to_be_traced.end()) {
    // This is a thread that is enqueued to be traced
    auto &delegate = *to_be_traced_it->second;
    _traced_threads.emplace(
        child_thread_id, std::move(to_be_traced_it->second));
    _ancestor_threads.emplace(
        child_thread_id, Ancestor(child_thread_id, &delegate));
    _to_be_traced.erase(to_be_traced_it);
    delegate.newThread(parent_thread_id, child_thread_id, pid);
    return;
  }
}

void ProcessTracer::terminateThread(uintptr_t thread_id) {
  auto ancestor_it = _ancestor_threads.find(thread_id);
  if (ancestor_it == _ancestor_threads.end()) {
    // The thread is not being traced.
    return;
  }

  // Call terminateThread before the object is potentially deleted below
  ancestor_it->second.delegate->terminateThread(thread_id);
  _ancestor_threads.erase(ancestor_it);

  auto delegate_it = _traced_threads.find(thread_id);
  if (delegate_it != _traced_threads.end()) {
    // This thread is an ancestor traced thread. Finish the tracing by
    // destroying the delegate.
    _traced_threads.erase(delegate_it);
  }
}

void ProcessTracer::fileEvent(
    uintptr_t thread_id, EventType type, std::string &&path) {
  if (auto delegate = findAncestor(thread_id).delegate) {
    delegate->fileEvent(thread_id, type, std::move(path));
  }
}

ProcessTracer::Ancestor ProcessTracer::findAncestor(uintptr_t thread_id) {
  auto ancestor_it = _ancestor_threads.find(thread_id);
  return ancestor_it == _ancestor_threads.end() ?
      Ancestor(thread_id, nullptr) : ancestor_it->second;
}


}  // namespace shk
