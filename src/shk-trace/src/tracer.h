#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <dispatch/dispatch.h>
#include <libc.h>

#include "dispatch.h"
#include "event.h"
#include "event_info.h"
#include "kdebug.h"
#include "kdebug_controller.h"
#include "syscall_constants.h"
#include "syscall_tables.h"

namespace shk {

/**
 * Tracer uses a low-level KdebugController object and (via the delegate)
 * exposes a higher-level stream of events (such as thread creation/termination
 * and file manipulation events). It does not format its output and it does not
 * follow process children.
 */
class Tracer {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;

    virtual void newThread(
        uintptr_t parent_thread_id,
        uintptr_t child_thread_id,
        pid_t pid) = 0;

    virtual void terminateThread(uintptr_t thread_id) = 0;

    virtual void fileEvent(
        uintptr_t thread_id, EventType type, std::string &&path) = 0;

    /**
     * Invoked whenever a file descriptor to a file or directory has been
     * opened, along with the path of the file (possibly relative), its
     * initial cloexec flag and the file descriptor that the path is relative
     * to (possibly AT_FDCWD which means the working directory).
     *
     * For some operations, Tracer will call both fileEvent and open.
     *
     * thread_id is the id of the thread that made the system call that caused
     * this to happen.
     */
    virtual void open(
        uintptr_t thread_id,
        pid_t pid,
        int fd,
        int at_fd,
        std::string &&path,
        bool cloexec) = 0;

    /**
     * Invoked whenever a file descriptor has been duplicated.
     *
     * thread_id is the id of the thread that made the system call that caused
     * this to happen.
     */
    virtual void dup(
        uintptr_t thread_id, pid_t pid, int from_fd, int to_fd) = 0;

    /**
     * Invoked whenever the cloexec flag has been set on a file descriptor.
     *
     * thread_id is the id of the thread that made the system call that caused
     * this to happen.
     */
    virtual void setCloexec(
        uintptr_t thread_id, pid_t pid, int fd, bool cloexec) = 0;

    /**
     * Invoked whenever a process has forked.
     *
     * thread_id is the id of the thread that made the system call that caused
     * this to happen.
     */
    virtual void fork(uintptr_t thread_id, pid_t ppid, pid_t pid) = 0;

    /**
     * Invoked whenever a file descriptor has been closed. (Except for when they
     * are implicitly closed due to exec.)
     *
     * thread_id is the id of the thread that made the system call that caused
     * this to happen.
     */
    virtual void close(uintptr_t thread_id, pid_t pid, int fd) = 0;

    /**
     * Invoked whenever a process's working directory has been changed. The path
     * may be relative. The file descriptor that the path is relative to is also
     * passed, in at_fd (which may be AT_FDCWD which means the current working
     * directory).
     *
     * thread_id is the id of the thread that made the system call that caused
     * this to happen.
     */
    virtual void chdir(
        uintptr_t thread_id, pid_t pid, std::string &&path, int at_fd) = 0;

    /**
     * Invoked whenever a thread has changed its thread-local working directory.
     * The path may be relative. If so, it is relative to the file pointed to by
     * the file descriptor at_fd (which may be AT_FDCWD whic means the current
     * working directory).
     */
    virtual void threadChdir(
        uintptr_t thread_id, std::string &&path, int at_fd) = 0;

    /**
     * Invoked whenever a process has successfully invoked an exec family system
     * call.
     *
     * thread_id is the id of the thread that made the system call that caused
     * this to happen.
     */
    virtual void exec(uintptr_t thread_id, pid_t pid) = 0;
  };

  Tracer(
      int num_cpus,
      KdebugController &kdebug_ctrl,
      Delegate &delegate);
  Tracer(const Tracer &) = delete;
  Tracer &operator=(const Tracer &) = delete;
  ~Tracer();

  void start(dispatch_queue_t queue);

 private:
  void loop(dispatch_queue_t queue);

  void set_enable(bool enabled);
  void set_remove();
  uint64_t sample_sc(std::vector<kd_buf> &event_buffer);
  void enter_event_now(uintptr_t thread, int type, kd_buf *kd, const char *name);
  void enter_event(uintptr_t thread, int type, kd_buf *kd, const char *name);
  void enter_illegal_event(uintptr_t thread, int type);
  void exit_event(
      uintptr_t thread,
      int type,
      uintptr_t arg1,
      uintptr_t arg2,
      uintptr_t arg3,
      uintptr_t arg4,
      const bsd_syscall &syscall);
  void format_print(
      event_info *ei,
      uintptr_t thread,
      int type,
      uintptr_t arg1,
      uintptr_t arg2,
      uintptr_t arg3,
      uintptr_t arg4,
      const bsd_syscall &syscall,
      const char *pathname /* nullable */);
  void create_map_entry(uintptr_t thread);
  void init_arguments_buffer();
  int get_real_command_name(int pid, char *cbuf, int csize);

  struct threadmap_entry {
    unsigned int tm_setsize = 0; // This is a bit count
    unsigned long *tm_setptr = nullptr;  // File descriptor bitmap
  };

  std::atomic<bool> _shutting_down;
  DispatchSemaphore _shutdown_semaphore;
  std::vector<kd_buf> _event_buffer;

  KdebugController &_kdebug_ctrl;
  Delegate &_delegate;

  std::unordered_map<uint64_t, std::string> _vn_name_map;
  event_info_map _ei_map;

  int _trace_enabled = 0;
};

}
