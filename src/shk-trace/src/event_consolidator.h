#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include <util/shktrace.h>

namespace shk {

/**
 * An EventConsolidator takes a stream of file system-related events (probably
 * originating from a Tracer) and consolidates them: Multiple reads to a given
 * file are treated as one. Same with writes. Files that are created and then
 * deleted are removed (in this use case we don't care about temporary files
 * that were removed).
 */
class EventConsolidator {
 public:
  EventConsolidator() = default;
  EventConsolidator(const EventConsolidator &) = default;
  EventConsolidator &operator=(const EventConsolidator &) = default;

  void event(EventType type, std::string &&path);

  using Event = std::pair<EventType, std::string>;

  /**
   * Calling this resets the state of the EventConsolidator. This reset is done
   * to allow this method to move out all the strings instead of copying them.
   */
  std::vector<Event> getConsolidatedEventsAndReset();

 private:
  std::unordered_set<std::string> _fatal_errors;
  std::unordered_set<std::string> _deleted;
  std::unordered_set<std::string> _created;
  std::unordered_set<std::string> _read;
  std::unordered_set<std::string> _read_directories;
  std::unordered_set<std::string> _written;
};

}  // namespace shk
