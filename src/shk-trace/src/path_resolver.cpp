#include "path_resolver.h"

#include <fcntl.h>

namespace shk {

PathResolver::PathResolver(
    Delegate &delegate, pid_t initial_pid, std::string &&initial_cwd)
    : _delegate(delegate),
      _cwd_memo(initial_pid, std::move(initial_cwd)) {}

void PathResolver::newThread(
    pid_t pid,
    uintptr_t parent_thread_id,
    uintptr_t child_thread_id) {
  _cwd_memo.newThread(parent_thread_id, child_thread_id);
}

void PathResolver::terminateThread(uintptr_t thread_id) {
  _cwd_memo.threadExit(thread_id);
}

void PathResolver::fileEvent(
    pid_t pid,
    uintptr_t thread_id,
    EventType type,
    int at_fd,
    std::string &&path) {
  _delegate.fileEvent(
      pid,
      thread_id,
      type,
      AT_FDCWD,  // Does not matter, since we pass an absolute path
      resolve(pid, thread_id, at_fd, std::move(path)));
}

void PathResolver::open(
    pid_t pid,
    uintptr_t thread_id,
    int fd,
    int at_fd,
    std::string &&path,
    bool cloexec) {
  _file_descriptor_memo.open(
      pid, fd, resolve(pid, thread_id, at_fd, std::move(path)), cloexec);
}

void PathResolver::dup(
    pid_t pid, uintptr_t thread_id, int from_fd, int to_fd, bool cloexec) {
  _file_descriptor_memo.dup(pid, from_fd, to_fd, cloexec);
}

void PathResolver::setCloexec(
    pid_t pid, uintptr_t thread_id, int fd, bool cloexec) {
  _file_descriptor_memo.setCloexec(pid, fd, cloexec);
}

void PathResolver::fork(pid_t ppid, uintptr_t thread_id, pid_t pid) {
  _file_descriptor_memo.fork(ppid, pid);
  _cwd_memo.fork(ppid, pid);
}

void PathResolver::close(pid_t pid, uintptr_t thread_id, int fd) {
  _file_descriptor_memo.close(pid, fd);
}

void PathResolver::chdir(
    pid_t pid, uintptr_t thread_id, std::string &&path, int at_fd) {
  _cwd_memo.chdir(pid, resolve(pid, thread_id, at_fd, std::move(path)));
}

void PathResolver::threadChdir(
    pid_t pid, uintptr_t thread_id, std::string &&path, int at_fd) {
  _cwd_memo.threadChdir(
      thread_id,
      resolve(pid, thread_id, at_fd, std::move(path)));
}

void PathResolver::exec(pid_t pid, uintptr_t thread_id) {
  // TODO(peck): Does exec terminate all threads of a process? If so, do we need
  // to _cwd_memo.threadExit() all those threads to not potentially leak memory?
  _file_descriptor_memo.exec(pid);
}

std::string PathResolver::resolve(
    pid_t pid, uintptr_t thread_id, int at_fd, std::string &&path) {
  if (path.size() && path[0] == '/') {
    // Path is absolute
    return path;
  }

  const auto &cwd = at_fd == AT_FDCWD ?
      _cwd_memo.getCwd(pid, thread_id) :
      _file_descriptor_memo.getFileDescriptorPath(pid, at_fd);

  return cwd + "/" + path;
}


}  // namespace shk
