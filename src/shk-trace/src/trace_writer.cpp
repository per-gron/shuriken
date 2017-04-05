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
