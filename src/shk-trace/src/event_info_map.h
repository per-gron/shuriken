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

#include <unordered_map>

#include "apsl_code.h"

namespace shk {

class EventInfoMap {
  struct TracedThread {
    /**
     * The last event type for this thread.
     */
    int last_event{};
    std::unordered_map<int, EventInfo> events;
  };

  using Map = std::unordered_map<uintptr_t, TracedThread>;

 public:
  void erase(uintptr_t thread, int type);

  /**
   * A new thread that should be traced has been created. This instructs
   * EventInfoMap to actually trace this thread. Threads that are not registered
   * with this call are not traced.
   *
   * This must be called only once per thread. (It is allowed to call this for
   * a given thread id again after it has been reclaimed with terminateThread.)
   */
  void newThread(uintptr_t thread);

  /**
   * Instruct EventInfoMap to cease tracing a thread. All events traced for this
   * thread must have been erased before making this call. (This requirement is
   * mostly there as a sanity check to verify that Kdebug is not playing games
   * with us.)
   *
   * It is legal to call this with a thread id that is not being traced. Such
   * calls are ignored.
   */
  void terminateThread(uintptr_t thread);

  /**
   * Returns nullptr if the given thread is not being traced.
   */
  EventInfo *addEvent(uintptr_t thread, int type);

  /**
   * Returns nullptr when not found.
   */
  EventInfo *find(uintptr_t thread, int type);

  EventInfo *findLast(uintptr_t thread);

 private:
  // Map from thread id to information for that thread. An entry in this map
  // indicates that the thread exists and has been requested to be traced.
  Map _map;
};

}  // namespace shk
