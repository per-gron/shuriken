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

#include "cwd_memo.h"
#include "file_descriptor_memo.h"
#include "tracer.h"

namespace shk {

/**
 * PathResolver is a Tracer::Delegate that receives tracing events with
 * potentially relative paths and resolves them. It forwards the events, with
 * only absolute paths, to another Tracer::Delegate.
 */
class PathResolver : public Tracer::Delegate {
 public:
  /**
   * PathResolver::Delegate is a stripped down version of Tracer::Delegate. Its
   * purpose is to allow PathResolver to not copy strings to callbacks that
   * aren't going to be used anyway.
   */
  class Delegate {
   public:
    virtual ~Delegate() = default;

    virtual void fileEvent(
        EventType type,
        std::string &&path) = 0;
  };

  PathResolver(
      std::unique_ptr<Delegate> &&delegate,
      pid_t initial_pid,
      std::string &&initial_cwd);

  virtual NewThreadResponse newThread(
      pid_t pid,
      uintptr_t parent_thread_id,
      uintptr_t child_thread_id) override;

  virtual TerminateThreadResponse terminateThread(uintptr_t thread_id) override;

  virtual void fileEvent(
    uintptr_t thread_id,
    EventType type,
    int at_fd,
    std::string &&path) override;

  virtual void open(
      uintptr_t thread_id,
      int fd,
      int at_fd,
      std::string &&path,
      bool cloexec) override;
  virtual void dup(
      uintptr_t thread_id,
      int from_fd,
      int to_fd,
      bool cloexec) override;
  virtual void setCloexec(
      uintptr_t thread_id, int fd, bool cloexec) override;
  virtual void close(uintptr_t thread_id, int fd) override;
  virtual void chdir(
      uintptr_t thread_id, std::string &&path, int at_fd) override;
  virtual void threadChdir(
      uintptr_t thread_id, std::string &&path, int at_fd) override;
  virtual void exec(uintptr_t thread_id) override;

 private:
  std::string resolve(
      uintptr_t thread_id, int at_fd, std::string &&path);

  const pid_t *getPid(uintptr_t thread_id) const;

  std::unique_ptr<Delegate> _delegate;
  std::unordered_map<uintptr_t, pid_t> _pids;
  CwdMemo _cwd_memo;
  FileDescriptorMemo _file_descriptor_memo;
};

}  // namespace shk
