#pragma once

#include "event.h"
#include "tracing_server.h"

namespace shk {

class TraceWriter : public PathResolver::Delegate {
 public:
  TraceWriter(std::unique_ptr<TracingServer::TraceRequest> &&request)
      : _request(std::move(request)) {}

  virtual ~TraceWriter() {
    auto events = _consolidator.getConsolidatedEventsAndReset();
    for (const auto &event : events) {
      write(eventTypeToString(event.first) + (" " + event.second) + "\n");
    }
  }

  virtual void fileEvent(
      uintptr_t thread_id,
      EventType type,
      int at_fd,
      std::string &&path) override {
    _consolidator.event(type, std::move(path));
  }

 private:
  void write(const std::string &str) {
    auto written = ::write(
        _request->trace_fd.get(),
        str.c_str(),
        str.size());
    if (written != str.size()) {
      fprintf(stderr, "Failed to write to tracing file\n");
      abort();
    }
  }

  // This object is destroyed when tracing has finished. That, in turn, will
  // destroy the TraceRequest, which signals to the traced process that tracing
  // has finished.
  const std::unique_ptr<TracingServer::TraceRequest> _request;
  EventConsolidator _consolidator;
};

}  // namespace shk
