// Copyright 2017 Per Grön. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <dispatch/dispatch.h>
#include <libc.h>

#include <util/shktrace.h>

#include "dispatch.h"
#include "event_info_map.h"
#include "event_type.h"
#include "syscall_constants.h"

namespace shk {

/**
 * Tracer receives data from Kdebug, parses it into a higher-level stream of
 * events (such as thread creation/termination and file manipulation events) and
 * emits parsed events to its Delegate. It does not format its output and it
 * does not follow process children.
 *
 * Tracer is intended to be used together with a KdebugPump object; it's
 * parseBuffer method matches the KdebugPump::Callback signature.
 */
class Tracer {
 public:
  class Delegate {
   public:
    enum class Response {
      OK,
      QUIT_TRACING
    };

    virtual ~Delegate() = default;

    virtual void newThread(
        pid_t pid,
        uintptr_t parent_thread_id,
        uintptr_t child_thread_id) = 0;

    /**
     * Invoked when a thread has terminated. The return value of this callback
     * gives an opportunity for the Delegate to instruct the tracer to stop.
     */
    virtual Response terminateThread(uintptr_t thread_id) = 0;

    /**
     * A path that is "" means that the path refers to the file or directory ´
     * that at_fd points to.
     */
    virtual void fileEvent(
        uintptr_t thread_id,
        EventType type,
        int at_fd,
        std::string &&path) = 0;

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
        uintptr_t thread_id,
        int from_fd,
        int to_fd,
        bool cloexec) = 0;

    /**
     * Invoked whenever the cloexec flag has been set on a file descriptor.
     *
     * thread_id is the id of the thread that made the system call that caused
     * this to happen.
     */
    virtual void setCloexec(
        uintptr_t thread_id, int fd, bool cloexec) = 0;

    /**
     * Invoked whenever a file descriptor has been closed. (Except for when they
     * are implicitly closed due to exec.)
     *
     * thread_id is the id of the thread that made the system call that caused
     * this to happen.
     */
    virtual void close(uintptr_t thread_id, int fd) = 0;

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
        uintptr_t thread_id, std::string &&path, int at_fd) = 0;

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
    virtual void exec(uintptr_t thread_id) = 0;
  };

  Tracer(Delegate &delegate);

  /**
   * Returns true if tracing should stop.
   */
  bool parseBuffer(const kd_buf *begin, const kd_buf *end);
 private:
  void processNewThread(const kd_buf &kd);
  /**
   * Returns true if tracing should stop.
   */
  bool processThreadTerminate(const kd_buf &kd);
  void processExec(const kd_buf &kd);
  void processVfsLookup(const kd_buf &kd);
  void processEventStart(uintptr_t thread, int type, const kd_buf &kd);
  void processEventEnd(
      uintptr_t thread,
      int type,
      uintptr_t arg1,
      uintptr_t arg2,
      uintptr_t arg3,
      uintptr_t arg4,
      int syscall);
  void notifyDelegate(
      EventInfo *ei,
      uintptr_t thread,
      int type,
      uintptr_t arg1,
      uintptr_t arg2,
      uintptr_t arg3,
      uintptr_t arg4,
      int syscall,
      const char *pathname1 /* nullable */,
      const char *pathname2 /* nullable */);

  Delegate &_delegate;

  std::unordered_map<uint64_t, std::string> _vn_name_map;
  EventInfoMap _ei_map;
};

}
