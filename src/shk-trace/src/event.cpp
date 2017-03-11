#include "event.h"

#include <type_traits>

namespace shk {

const char *eventTypeToString(EventType event_type) {
  static constexpr const char *event_types[] = {
    "read",
    "write",
    "create",
    "delete",
    "fatal_error"
  };
  return event_types[
      static_cast<std::underlying_type<EventType>::type>(event_type)];
}

}  // namespace shk
