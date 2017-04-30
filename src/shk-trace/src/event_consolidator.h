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
  std::unordered_set<std::string> _inputs;
  std::unordered_set<std::string> _outputs;
  /**
   * Files that have been deleted and that are not yet overwritten. This is used
   * to keep track of if a process deletes files that it did not create.
   */
  std::unordered_set<std::string> _deleted;
  std::vector<std::string> _errors;
};

}  // namespace shk
