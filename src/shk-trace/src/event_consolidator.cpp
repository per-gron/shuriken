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

#include "event_consolidator.h"

namespace shk {

void EventConsolidator::event(EventType type, std::string &&path) {
  switch (type) {
  case EventType::READ:
  case EventType::READ_DIRECTORY:
    if (_outputs.count(path) == 0) {
      // When a program reads from a file that it created itself, that doesn't
      // affect the result of the program; it can only see what it itself has
      // written.
      _inputs.insert(std::move(path));
    }
    break;

  case EventType::WRITE:
    {
      auto it = _inputs.find(path);
      if (it != _inputs.end()) {
        // Ideally, this should be an error. However, it is very common that
        // programs stat the path of their output before writing to it so it's
        // not feasible to fail because of this.

        // A file should be either an input or an output, not both
        _inputs.erase(it);
      }
      _outputs.emplace(std::move(path));
    }
    break;

  case EventType::CREATE:
    {
      auto it = _inputs.find(path);
      if (it != _inputs.end()) {
        // Ideally, this should be an error. However, it is very common that
        // programs stat the path of their output before writing to it so it's
        // not feasible to fail because of this.

        // A file should be either an input or an output, not both
        _inputs.erase(it);
      }
      _deleted.erase(path);
      _outputs.emplace(std::move(path));
    }
    break;

  case EventType::DELETE:
    {
      auto it = _outputs.find(path);
      if (it == _outputs.end()) {
        _deleted.insert(path);
      } else {
        _outputs.erase(it);
      }
    }
    break;

  case EventType::FATAL_ERROR:
    _errors.push_back(std::move(path));
    break;
  }
}

flatbuffers::Offset<Trace> EventConsolidator::generateTrace(
    flatbuffers::FlatBufferBuilder &builder) const {
  // inputs
  std::vector<flatbuffers::Offset<flatbuffers::String>> input_offsets;
  input_offsets.reserve(_inputs.size());

  for (const auto &input : _inputs) {
    input_offsets.push_back(builder.CreateString(input));
  }
  auto input_vector = builder.CreateVector(
      input_offsets.data(), input_offsets.size());

  // outputs
  std::vector<flatbuffers::Offset<flatbuffers::String>> output_offsets;
  output_offsets.reserve(_outputs.size());

  for (const auto &output : _outputs) {
    output_offsets.push_back(builder.CreateString(output));
  }
  auto output_vector = builder.CreateVector(
      output_offsets.data(), output_offsets.size());

  // errors
  std::vector<flatbuffers::Offset<flatbuffers::String>> error_offsets;
  error_offsets.reserve(_errors.size());

  for (const auto &error : _errors) {
    error_offsets.push_back(builder.CreateString(error));
  }

  for (const auto &deleted : _deleted) {
    if (_outputs.count(deleted) == 0) {
      error_offsets.push_back(builder.CreateString(
          "Process deleted file it did not create: " + deleted));
    }
  }

  auto error_vector = builder.CreateVector(
      error_offsets.data(), error_offsets.size());

  return CreateTrace(builder, input_vector, output_vector, error_vector);
}

}  // namespace shk
