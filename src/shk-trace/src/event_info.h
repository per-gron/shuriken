#pragma once

#include <unordered_map>

#include "apsl_code.h"

namespace shk {

class EventInfoMap {
  using PerThreadMap = std::unordered_map<int, EventInfo>;
  using Map = std::unordered_map<uintptr_t, PerThreadMap>;

 public:
  using iterator = std::pair<Map::iterator, PerThreadMap::iterator>;

  void erase(uintptr_t thread, int type);

  void verifyNoEventsForThread(uintptr_t thread) const;

  EventInfo &addEvent(uintptr_t thread, int type);

  /**
   * Returns nullptr when not found.
   */
  EventInfo *find(uintptr_t thread, int type);

  EventInfo *findLast(uintptr_t thread);

 private:
  Map _map;
  // Map from thread id to last event type for that thread
  std::unordered_map<uintptr_t, int> _last_event_map;
};

}  // namespace shk
