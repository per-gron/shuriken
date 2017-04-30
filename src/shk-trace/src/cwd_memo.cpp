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

#include "cwd_memo.h"

namespace shk {

CwdMemo::CwdMemo(pid_t initial_pid, std::string &&initial_cwd) {
  _process_cwds[initial_pid] = std::move(initial_cwd);
}

void CwdMemo::fork(pid_t ppid, uintptr_t parent_thread_id, pid_t pid) {
  _process_cwds[pid] = getCwd(ppid, parent_thread_id);
}

void CwdMemo::chdir(pid_t pid, std::string &&path) {
  _process_cwds[pid] = std::move(path);
}

void CwdMemo::exit(pid_t pid) {
  _process_cwds.erase(pid);
}

void CwdMemo::newThread(uintptr_t parent_thread_id, uintptr_t child_thread_id) {
}

void CwdMemo::threadChdir(uintptr_t thread_id, std::string &&path) {
  _thread_cwds[thread_id] = std::move(path);
}

void CwdMemo::threadExit(uintptr_t thread_id) {
  _thread_cwds.erase(thread_id);
}

const std::string &CwdMemo::getCwd(pid_t pid, uintptr_t thread_id) const {
  static const std::string empty;

  auto thread_it = _thread_cwds.find(thread_id);
  if (thread_it != _thread_cwds.end()) {
    return thread_it->second;
  }

  auto process_it = _process_cwds.find(pid);
  if (process_it != _process_cwds.end()) {
    return process_it->second;
  }

  return empty;
}

}  // namespace shk
