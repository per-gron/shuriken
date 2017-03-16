#include "file_descriptor_memo.h"

#include <vector>

namespace shk {

void FileDescriptorMemo::open(
    pid_t pid, int fd, std::string &&path, bool cloexec) {
  _processes[pid][fd] = FDInfo{ std::move(path), cloexec };
}

void FileDescriptorMemo::close(pid_t pid, int fd) {
  auto &process_info = _processes[pid];
  process_info.erase(fd);
  if (process_info.empty()) {
    _processes.erase(pid);
  }
}

void FileDescriptorMemo::dup(pid_t pid, int from_fd, int to_fd, bool cloexec) {
  auto process_it = _processes.find(pid);
  if (process_it == _processes.end()) {
    // Unknown pid. Nothing to dup.
    return;
  }

  auto from_it = process_it->second.find(from_fd);
  if (from_it == process_it->second.end()) {
    // We don't know about this fd. Perhaps it's a socket. Anyway, there is
    // nothing we can do.
    return;
  }

  process_it->second[to_fd] = FDInfo{ from_it->second.path, cloexec };
}

void FileDescriptorMemo::exec(pid_t pid) {
  auto process_it = _processes.find(pid);
  if (process_it == _processes.end()) {
    // Unknown pid. Nothing to dup.
    return;
  }

  auto &process_info = process_it->second;

  std::vector<int> fds_to_close;
  for (auto &fd : process_info) {
    if (fd.second.cloexec) {
      fds_to_close.push_back(fd.first);
    }
  }
  for (int fd : fds_to_close) {
    process_info.erase(fd);
  }
}

void FileDescriptorMemo::fork(pid_t ppid, pid_t pid) {
  auto ppid_it = _processes.find(ppid);
  if (ppid_it != _processes.end()) {
    _processes[pid] = ppid_it->second;
  }
}

void FileDescriptorMemo::setCloexec(pid_t pid, int fd, bool cloexec) {
  auto process_it = _processes.find(pid);
  if (process_it != _processes.end()) {
    auto fd_it = process_it->second.find(fd);
    if (fd_it != process_it->second.end()) {
      fd_it->second.cloexec = cloexec;
    }
  }
}

void FileDescriptorMemo::terminated(pid_t pid) {
  _processes.erase(pid);
}

const std::string &FileDescriptorMemo::getFileDescriptorPath(
    pid_t pid, int fd) const {
  static const std::string empty;
  auto process_it = _processes.find(pid);
  if (process_it != _processes.end()) {
    auto fd_it = process_it->second.find(fd);
    if (fd_it != process_it->second.end()) {
      return fd_it->second.path;
    } else {
      return empty;
    }
  } else {
    return empty;
  }
}

}  // namespace shk
