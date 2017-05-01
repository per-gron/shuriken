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

#include "event_info_map.h"

namespace shk {

void EventInfoMap::erase(uintptr_t thread, int type) {
  auto map_iter = _map.find(thread);
  if (map_iter == _map.end()) {
    return;
  }
  auto &traced_thread = map_iter->second;

  if (traced_thread.last_event == type) {
    traced_thread.last_event = 0;
  }

  traced_thread.events.erase(type);
}

void EventInfoMap::newThread(uintptr_t thread) {
  const bool inserted = _map.emplace(thread, TracedThread()).second;
  if (!inserted) {
    throw std::runtime_error(
        "internal error: spawning already existing thread");
  }
}

void EventInfoMap::terminateThread(uintptr_t thread) {
  auto map_iter = _map.find(thread);
  if (map_iter == _map.end()) {
    return;
  }
  auto &traced_thread = map_iter->second;

  if (!traced_thread.events.empty() || traced_thread.last_event) {
    throw std::runtime_error("internal error: did not clean up");
  }

  _map.erase(map_iter);
}

EventInfo &EventInfoMap::addEvent(uintptr_t thread, int type) {
  auto map_iter = _map.find(thread);
  if (map_iter == _map.end()) {
    map_iter = _map.emplace(thread, TracedThread()).first;
  }
  auto &traced_thread = map_iter->second;

  traced_thread.last_event = type;

  return traced_thread.events[type] = EventInfo();
}

/**
 * Returns nullptr when not found.
 */
EventInfo *EventInfoMap::find(uintptr_t thread, int type) {
  auto map_iter = _map.find(thread);
  if (map_iter == _map.end()) {
    return nullptr;
  } else {
    auto &traced_thread = map_iter->second;
    auto it = traced_thread.events.find(type);
    return it == traced_thread.events.end() ?
        nullptr :
        &it->second;
  }
}

EventInfo *EventInfoMap::findLast(uintptr_t thread) {
  auto map_iter = _map.find(thread);
  return map_iter == _map.end() ?
      nullptr :
      find(thread, map_iter->second.last_event);
}

}  // namespace shk
