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
