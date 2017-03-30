#include "path_resolver.h"

#include <fcntl.h>

namespace shk {

PathResolver::PathResolver(
    std::unique_ptr<Delegate> &&delegate,
    pid_t initial_pid,
    std::string &&initial_cwd)
    : _delegate(std::move(delegate)),
      _cwd_memo(initial_pid, std::move(initial_cwd)) {}

void PathResolver::newThread(
    pid_t pid,
    uintptr_t parent_thread_id,
    uintptr_t child_thread_id) {
  _cwd_memo.newThread(parent_thread_id, child_thread_id);
  _pids[child_thread_id] = pid;

  if (auto ppid = getPid(parent_thread_id)) {
    _file_descriptor_memo.fork(*ppid, pid);
    _cwd_memo.fork(*ppid, pid);
  }
}

Tracer::Delegate::Response PathResolver::terminateThread(uintptr_t thread_id) {
  _cwd_memo.threadExit(thread_id);
  _pids.erase(thread_id);

  return Response::OK;
}

void PathResolver::fileEvent(
    uintptr_t thread_id,
    EventType type,
    int at_fd,
    std::string &&path) {
  _delegate->fileEvent(
      thread_id,
      type,
      AT_FDCWD,  // Does not matter, since we pass an absolute path
      type == EventType::FATAL_ERROR ?
          std::move(path) :
          resolve(thread_id, at_fd, std::move(path)));
}

void PathResolver::open(
    uintptr_t thread_id,
    int fd,
    int at_fd,
    std::string &&path,
    bool cloexec) {
  if (auto pid = getPid(thread_id)) {
    _file_descriptor_memo.open(
        *pid, fd, resolve(thread_id, at_fd, std::move(path)), cloexec);
  }
}

void PathResolver::dup(
    uintptr_t thread_id, int from_fd, int to_fd, bool cloexec) {
  if (auto pid = getPid(thread_id)) {
    _file_descriptor_memo.dup(*pid, from_fd, to_fd, cloexec);
  }
}

void PathResolver::setCloexec(
    uintptr_t thread_id, int fd, bool cloexec) {
  if (auto pid = getPid(thread_id)) {
    _file_descriptor_memo.setCloexec(*pid, fd, cloexec);
  }
}

void PathResolver::close(uintptr_t thread_id, int fd) {
  if (auto pid = getPid(thread_id)) {
    _file_descriptor_memo.close(*pid, fd);
  }
}

void PathResolver::chdir(
    uintptr_t thread_id, std::string &&path, int at_fd) {
  if (auto pid = getPid(thread_id)) {
    _cwd_memo.chdir(*pid, resolve(thread_id, at_fd, std::move(path)));
  }
}

void PathResolver::threadChdir(
    uintptr_t thread_id, std::string &&path, int at_fd) {
  _cwd_memo.threadChdir(
      thread_id,
      resolve(thread_id, at_fd, std::move(path)));
}

void PathResolver::exec(uintptr_t thread_id) {
  // TODO(peck): Does exec terminate all threads of a process? If so, do we need
  // to _cwd_memo.threadExit() all those threads to not potentially leak memory?
  if (auto pid = getPid(thread_id)) {
    _file_descriptor_memo.exec(*pid);
  }
}

std::string PathResolver::resolve(
    uintptr_t thread_id, int at_fd, std::string &&path) {
  if (path.size() && path[0] == '/') {
    // Path is absolute
    return path;
  }

  if (auto pid = getPid(thread_id)) {
    const auto &cwd = at_fd == AT_FDCWD ?
        _cwd_memo.getCwd(*pid, thread_id) :
        _file_descriptor_memo.getFileDescriptorPath(*pid, at_fd);

    return path.empty() ?
        cwd :
        cwd + (cwd.empty() || cwd[cwd.size() - 1] != '/' ? "/" : "") + path;
  } else {
    return path;
  }
}

const pid_t *PathResolver::getPid(uintptr_t thread_id) const {
  auto id = _pids.find(thread_id);
  return id == _pids.end() ? nullptr : &id->second;
}

}  // namespace shk
