#include "trace_writer.h"

#include <util/shktrace.h>

namespace shk {

TraceWriter::TraceWriter(std::unique_ptr<TracingServer::TraceRequest> &&request)
    : _request(std::move(request)) {}

TraceWriter::~TraceWriter() {
  auto events = _consolidator.getConsolidatedEventsAndReset();

  flatbuffers::FlatBufferBuilder builder(1024);
  std::vector<flatbuffers::Offset<Event>> event_pointers;
  event_pointers.reserve(events.size());

  for (const auto &event : events) {
    auto path_name = builder.CreateString(event.second);
    event_pointers.push_back(CreateEvent(builder, event.first, path_name));
  }
  auto event_vector = builder.CreateVector(
      event_pointers.data(), event_pointers.size());

  builder.Finish(CreateTrace(builder, event_vector));

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
    std::string &&path,
    SymlinkBehavior symlink_behavior) {
  _consolidator.event(type, std::move(path));
}

}  // namespace shk
