#include "process_tracer.h"

namespace shk {

void ProcessTracer::traceProcess(
    pid_t pid,
    uintptr_t root_thread_id,
    std::unique_ptr<Tracer::Delegate> &&delegate) {
  _to_be_traced.emplace(pid, ToBeTraced{ root_thread_id, std::move(delegate) });
}

void ProcessTracer::newThread(
    pid_t pid,
    uintptr_t parent_thread_id,
    uintptr_t child_thread_id) {
  if (Ancestor ancestor = findAncestor(parent_thread_id)) {
    // This is a child thread of an already traced thread.
    bool success = _ancestor_threads.emplace(child_thread_id, ancestor).second;
    if (!success) {
      throw std::runtime_error(
          "Created already existing thread. This should not happen.");
    }
    ancestor.delegate->newThread(pid, parent_thread_id, child_thread_id);
    return;
  }

  auto to_be_traced_it = _to_be_traced.find(pid);
  if (to_be_traced_it != _to_be_traced.end()) {
    // This is a thread for a process that is enqueued to be traced
    auto &to_be_traced = to_be_traced_it->second;
    if (child_thread_id == to_be_traced.root_thread_id) {
      // This is the thread creation event for the root thread of the process
      // to be traced. This is the thread that will wait for tracing to finish.
      // If we start tracing this one, tracing will deadlock.
    } else if (parent_thread_id != to_be_traced.root_thread_id) {
      // The parent thread of the spawned thread is not the root thread. This
      // means that it is for sure not the thread that this trace request
      // intended to trace.
      //
      // This case can be reached when pids get reused quickly and a process
      // makes a trace request while the tracing server (this process) is still
      // processing events from the old process. I think.
    } else {
      auto &delegate = *to_be_traced.delegate;
      _traced_threads.emplace(
          child_thread_id, std::move(to_be_traced.delegate));
      _ancestor_threads.emplace(
          child_thread_id, Ancestor(child_thread_id, &delegate));
      _to_be_traced.erase(to_be_traced_it);
      delegate.newThread(pid, parent_thread_id, child_thread_id);
    }
  }
}

Tracer::Delegate::Response ProcessTracer::terminateThread(uintptr_t thread_id) {
  auto ancestor_it = _ancestor_threads.find(thread_id);
  if (ancestor_it == _ancestor_threads.end()) {
    // The thread is not being traced.
    return Response::OK;
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

  return Response::OK;
}

void ProcessTracer::fileEvent(
    uintptr_t thread_id,
    EventType type,
    int at_fd,
    std::string &&path) {
  if (auto delegate = findAncestor(thread_id).delegate) {
    delegate->fileEvent(thread_id, type, at_fd, std::move(path));
  }
}

void ProcessTracer::open(
    uintptr_t thread_id,
    int fd,
    int at_fd,
    std::string &&path,
    bool cloexec) {
  if (auto delegate = findAncestor(thread_id).delegate) {
    delegate->open(thread_id, fd, at_fd, std::move(path), cloexec);
  }
}

void ProcessTracer::dup(
    uintptr_t thread_id, int from_fd, int to_fd, bool cloexec) {
  if (auto delegate = findAncestor(thread_id).delegate) {
    delegate->dup(thread_id, from_fd, to_fd, cloexec);
  }
}

void ProcessTracer::setCloexec(
    uintptr_t thread_id, int fd, bool cloexec) {
  if (auto delegate = findAncestor(thread_id).delegate) {
    delegate->setCloexec(thread_id, fd, cloexec);
  }
}

void ProcessTracer::close(uintptr_t thread_id, int fd) {
  if (auto delegate = findAncestor(thread_id).delegate) {
    delegate->close(thread_id, fd);
  }
}

void ProcessTracer::chdir(
    uintptr_t thread_id, std::string &&path, int at_fd) {
  if (auto delegate = findAncestor(thread_id).delegate) {
    delegate->chdir(thread_id, std::move(path), at_fd);
  }
}

void ProcessTracer::threadChdir(
    uintptr_t thread_id, std::string &&path, int at_fd) {
  if (auto delegate = findAncestor(thread_id).delegate) {
    delegate->threadChdir(thread_id, std::move(path), at_fd);
  }
}

void ProcessTracer::exec(uintptr_t thread_id) {
  if (auto delegate = findAncestor(thread_id).delegate) {
    delegate->exec(thread_id);
  }
}

ProcessTracer::Ancestor ProcessTracer::findAncestor(uintptr_t thread_id) {
  auto ancestor_it = _ancestor_threads.find(thread_id);
  return ancestor_it == _ancestor_threads.end() ?
      Ancestor(thread_id, nullptr) : ancestor_it->second;
}


}  // namespace shk
