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
#include <unistd.h>

namespace shk {

/**
 * When shk-trace traces syscalls with the purpose of finding the path of files
 * that are read and written by certain programs, the syscall data stream can
 * contain not only absolute but also relative paths. To be able to resolve
 * these relative paths, shk-trace keeps track of the current working directory
 * of each process (and each thread in each process).
 *
 * CwdMemo helps keeping track of current working directories of traced
 * processes.
 */
class CwdMemo {
 public:
  CwdMemo(pid_t initial_pid, std::string &&initial_cwd);

  /**
   * Call this after a process has successfully forked. It is a no-op to call
   * this method for an unknown ppid.
   */
  void fork(pid_t ppid, uintptr_t parent_thread_id, pid_t pid);

  /**
   * Change the process-wide cwd for a process. Call this when a process has
   * successfully changed its working directory. Calling this with a pid that
   * CwdMemo has not seen before causes it to start tracking that pid.
   */
  void chdir(pid_t pid, std::string &&path);

  /**
   * Forget about a process. Call this when a process has exited. It is a no-op
   * to call this with a pid that CwdMemo does not know about.
   */
  void exit(pid_t pid);

  /**
   * Call this when a new thread has been spawned. It is a no-op to call this
   * with a parent_thread_id that CwdMemo does not know about.
   */
  void newThread(uintptr_t parent_thread_id, uintptr_t child_thread_id);

  /**
   * Call this when a thread has changed its thread-local cwd. Calling this
   * with a thread id that CwdMemo has not seen before causes it to start
   * tracking this thread.
   */
  void threadChdir(uintptr_t thread_id, std::string &&path);

  /**
   * Call this when a thread has terminated. It is a no-op to call this with a
   * thread id that CwdMemo does not know about.
   */
  void threadExit(uintptr_t parent_thread_id);

  /**
   * Get the cwd for a given thread in a given process. Returns the empty string
   * if the cwd is not known.
   */
  const std::string &getCwd(pid_t pid, uintptr_t thread_id) const;

 private:
  std::unordered_map<pid_t, std::string> _process_cwds;
  std::unordered_map<uintptr_t, std::string> _thread_cwds;
};

}  // namespace shk
