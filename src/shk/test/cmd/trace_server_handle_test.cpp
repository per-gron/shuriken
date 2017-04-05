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
