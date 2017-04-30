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

#include "trace_writer.h"

#include <util/shktrace.h>

namespace shk {

TraceWriter::TraceWriter(std::unique_ptr<TracingServer::TraceRequest> &&request)
    : _request(std::move(request)) {}

TraceWriter::~TraceWriter() {
  flatbuffers::FlatBufferBuilder builder(1024);
  builder.Finish(_consolidator.generateTrace(builder));

  auto written = write(
      _request->trace_fd.get(),
      builder.GetBufferPointer(),
      builder.GetSize());
  if (written != builder.GetSize()) {
    fprintf(stderr, "Failed to write to tracing file\n");
    abort();
  }
}

void TraceWriter::fileEvent(
    EventType type,
    std::string &&path) {
  _consolidator.event(type, std::move(path));
}

}  // namespace shk
