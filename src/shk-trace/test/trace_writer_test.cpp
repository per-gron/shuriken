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

#include <catch.hpp>

#include "trace_writer.h"

namespace shk {

TEST_CASE("TraceWriter") {
  int fd[2];
  REQUIRE(pipe(fd) == 0);
  FileDescriptor input_fd(fd[0]);
  FileDescriptor output_fd(fd[1]);

  {
    TraceWriter writer(std::unique_ptr<TracingServer::TraceRequest>(
        new TracingServer::TraceRequest(std::move(output_fd), 0, 0, "cwd")));

    writer.fileEvent(EventType::READ, "path1");
    writer.fileEvent(EventType::CREATE, "path2");
  }

  char raw_buf[1024];
  int len = read(input_fd.get(), &raw_buf, 1024);

  auto trace = GetTrace(raw_buf);
  REQUIRE(trace->inputs()->size() == 1);
  CHECK(std::string(trace->inputs()->Get(0)->data()) == "path1");

  REQUIRE(trace->outputs()->size() == 1);
  CHECK(std::string(trace->outputs()->Get(0)->data()) == "path2");

  CHECK(trace->errors()->size() == 0);
}

}  // namespace shk
