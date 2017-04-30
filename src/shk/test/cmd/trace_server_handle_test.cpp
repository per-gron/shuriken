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

#include "cmd/trace_server_handle.h"

namespace shk {

TEST_CASE("TraceServerHandle") {
  SECTION("GetShkTracePath") {
    auto handle = TraceServerHandle::open("/nonexisting");
    CHECK(handle->getShkTracePath() == "/nonexisting");
  }

  SECTION("InvalidCommand") {
    auto handle = TraceServerHandle::open("/nonexisting");
    std::string err;
    CHECK(!handle->startServer(&err));
    CHECK(err == "posix_spawn() failed");
  }

  SECTION("WrongAcknowledgementMessage") {
    auto handle = TraceServerHandle::open("/bin/echo");
    std::string err;
    CHECK(!handle->startServer(&err));
    CHECK(err == "did not see expected acknowledgement message");
  }

  SECTION("Success") {
    auto handle = TraceServerHandle::open("shk-trace-dummy");
    std::string err;
    CHECK(handle->startServer(&err));
    CHECK(err == "");
  }

  SECTION("StartTwice") {
    auto handle = TraceServerHandle::open("shk-trace-dummy");
    std::string err;
    CHECK(handle->startServer(&err));
    CHECK(err == "");
    CHECK(handle->startServer(&err));
    CHECK(err == "");
  }
}

}  // namespace shk
