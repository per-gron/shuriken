#pragma once

#include "event.h"
#include "event_consolidator.h"
#include "path_resolver.h"
#include "tracing_server.h"

namespace shk {

class TraceWriter : public PathResolver::Delegate {
 public:
  TraceWriter(std::unique_ptr<TracingServer::TraceRequest> &&request);

  virtual ~TraceWriter();

  virtual void fileEvent(
      EventType type,
      std::string &&path) override;

 private:
  void write(const std::string &str);

  // This object is destroyed when tracing has finished. That, in turn, will
  // destroy the TraceRequest, which signals to the traced process that tracing
  // has finished.
  const std::unique_ptr<TracingServer::TraceRequest> _request;
  EventConsolidator _consolidator;
};

}  // namespace shk
