#include "trace_writer.h"

namespace shk {

TraceWriter::TraceWriter(std::unique_ptr<TracingServer::TraceRequest> &&request)
    : _request(std::move(request)) {}

TraceWriter::~TraceWriter() {
  auto events = _consolidator.getConsolidatedEventsAndReset();
  for (const auto &event : events) {
    write(eventTypeToString(event.first) + (" " + event.second) + "\n");
  }
}

void TraceWriter::fileEvent(
    EventType type,
    std::string &&path) {
  _consolidator.event(type, std::move(path));
}

void TraceWriter::write(const std::string &str) {
  auto written = ::write(
      _request->trace_fd.get(),
      str.c_str(),
      str.size());
  if (written != str.size()) {
    fprintf(stderr, "Failed to write to tracing file\n");
    abort();
  }
}

}  // namespace shk
