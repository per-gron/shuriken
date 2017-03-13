#pragma once

#include <string>
#include <unordered_map>
#include <unistd.h>

namespace shk {

/**
 * When shk-trace traces syscalls with the purpose of finding the path of files
 * that are read and written by certain programs, the syscall data stream can
 * contain not only absolute but also relative paths. To further complicate
 * things, the *at syscalls (like openat) can have relative paths that in turn
 * are relative to a file descriptor pointing to a directory.
 *
 * This means that shk-trace needs to trace open file descriptors of programs
 * that are being traced.
 *
 * Note: The logic of this class breaks down if traced programs or any other
 * program in the system moves around directories. However, the shk build system
 * (and probably any build system really), would break down under such
 * circumstances anyway.
 */
class FileDescriptorMemo {
 public:
  /**
   * Call when a file descriptor to a given (absolute) path has been
   * successfully opened. It is not necessary to call it for file descriptors
   * that do not refer to paths, even if those fds are used later in calls to
   * dup/close etc. They will just be ignored.
   */
  void open(pid_t pid, int fd, std::string &&path, bool cloexec);

  /**
   * Call when a file descriptor has been closed. Calling close with a
   * nonexisting or unknown fd is a no-op.
   */
  void close(pid_t pid, int fd);

  /**
   * Call when a file descriptor has been duplicated. Calling dup with a from_fd
   * that does not exist or is unknown is a no-op.
   */
  void dup(pid_t pid, int from_fd, int to_fd);

  /**
   * Call when a process has done an exec family syscall. Closes fds that are
   * marked with the cloexec flag. Calling exec for a pid that has not been
   * mentioned before i a no-op.
   */
  void exec(pid_t pid);

  /**
   * Call when a process has forked in such a way that it gets a new pid and
   * that file descriptors are shared between the processes.
   */
  void fork(pid_t ppid, pid_t pid);

  /**
   * Call when the cloexec flag has been modified for a given file descriptor.
   * Calling setCloexec for a fd that doesn't exist or is unknown is a no-op.
   */
  void setCloexec(pid_t pid, int fd, bool cloexec);

  /**
   * Call when a process has terminated. Cleans up resources for that pid.
   * Calling terminated for a pid that has not previously been mentioned is a
   * no-op.
   */
  void terminated(pid_t pid);

  /**
   * Returns an empty string if the file descriptor is not known.
   */
  const std::string &getFileDescriptorPath(pid_t pid, int fd) const;

 private:
  struct FDInfo {
    std::string path;
    bool cloexec;
  };

  // Map from file descriptor to info about that file descriptor
  using ProcessInfo = std::unordered_map<int, FDInfo>;

  // Map from pid to fds for a given process
  std::unordered_map<pid_t, ProcessInfo> _processes;
};

}  // namespace shk
