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
  auto last_event_it = _last_event_map.find(thread);
  if (last_event_it != _last_event_map.end() && last_event_it->second == type) {
    _last_event_map.erase(last_event_it);
  }

  auto map_iter = _map.find(thread);
  if (map_iter == _map.end()) {
    return;
  }

  auto &per_thread_map = map_iter->second;
  per_thread_map.erase(type);
  if (per_thread_map.empty()) {
    _map.erase(thread);
  }
}

void EventInfoMap::verifyNoEventsForThread(uintptr_t thread) const {
  if (_map.count(thread) != 0) {
    throw std::runtime_error("internal error: did not clean up");
  }
  if (_last_event_map.count(thread)) {
    throw std::runtime_error("internal error: did not clean up last event");
  }
}

EventInfo &EventInfoMap::addEvent(uintptr_t thread, int type) {
  _last_event_map[thread] = type;

  auto map_iter = _map.find(thread);
  if (map_iter == _map.end()) {
    map_iter = _map.emplace(thread, PerThreadMap()).first;
  }

  auto &per_thread_map = map_iter->second;

  return per_thread_map[type] = EventInfo();
}

/**
 * Returns nullptr when not found.
 */
EventInfo *EventInfoMap::find(uintptr_t thread, int type) {
  auto map_iter = _map.find(thread);
  if (map_iter == _map.end()) {
    return nullptr;
  } else {
    auto &per_thread_map = map_iter->second;
    auto it = per_thread_map.find(type);
    return it == per_thread_map.end() ?
        nullptr :
        &it->second;
  }
}

EventInfo *EventInfoMap::findLast(uintptr_t thread) {
  auto it = _last_event_map.find(thread);
  return it == _last_event_map.end() ?
      nullptr :
      find(thread, it->second);
}

}  // namespace shk
