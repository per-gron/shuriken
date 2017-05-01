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

  void verifyNoEventsForThread(uintptr_t thread) const;

  EventInfo &addEvent(uintptr_t thread, int type);

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
