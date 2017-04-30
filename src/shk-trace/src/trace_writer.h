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

#include <util/shktrace.h>

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
  // This object is destroyed when tracing has finished. That, in turn, will
  // destroy the TraceRequest, which signals to the traced process that tracing
  // has finished.
  const std::unique_ptr<TracingServer::TraceRequest> _request;
  EventConsolidator _consolidator;
};

}  // namespace shk
