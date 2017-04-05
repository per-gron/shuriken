#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <util/shktrace.h>

#include "event_type.h"

namespace shk {

/**
 * An EventConsolidator takes a stream of file system-related events (probably
 * originating from a Tracer) and consolidates them into a Trace flatbuffer:
 *
 * * A list of input files
 * * A list of output files
 * * Errors
 *
 * Creating a file causes it to be an output file, deleting a file that was not
 * created is an error, etc.
 */
class EventConsolidator {
 public:
  EventConsolidator() = default;
  EventConsolidator(const EventConsolidator &) = default;
  EventConsolidator &operator=(const EventConsolidator &) = default;

  void event(EventType type, std::string &&path);

  flatbuffers::Offset<Trace> generateTrace(
      flatbuffers::FlatBufferBuilder &builder) const;

 private:
  /**
   * Map from path => bool indicating if the path is a directory whose files
   * were listed.
   */
  std::unordered_map<std::string, bool> _inputs;
  /**
   * Map from path => bool indicating if the file was entirely overwritten.
   */
  std::unordered_map<std::string, bool> _outputs;
  std::unordered_set<std::string> _deleted;
  std::vector<std::string> _errors;
};

}  // namespace shk
