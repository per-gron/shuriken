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
#include <unistd.h>

namespace shk {

/**
 * This is a helper class that exposes functionality to spawn a shk-trace server
 * process, wait for it to start serving and then sending a SIGTERM to it when
 * destroying the object.
 *
 * The public base class TraceServerHandle is a no-op class that is useful for
 * mocking. To get an instance that actually does something, use
 * TraceServerHandle::open.
 */
class TraceServerHandle {
 public:
  TraceServerHandle() = default;
  TraceServerHandle(const TraceServerHandle &) = delete;
  TraceServerHandle &operator=(const TraceServerHandle &) = delete;
  virtual ~TraceServerHandle() = default;

  virtual const std::string &getShkTracePath() = 0;

  /**
   * Start the shk-trace server. Returns false on failure.
   *
   * Calling this method again after it has succeeded once is a no-op.
   */
  virtual bool startServer(std::string *err) = 0;

  /**
   * Returns a nullptr unique_ptr on failure.
   *
   * shk_trace_command is the shk-trace command that should be run. It is
   * recommended to let it be of the format "shk-trace -s -O" to enable server
   * mode and to enable suicide-when-orphaned behavior, to avoid having the
   * server process laying around for longer than this process.
   */
  static std::unique_ptr<TraceServerHandle> open(
      const std::string &shk_trace_command);
};

}  // namespace shk
