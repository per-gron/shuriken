#include <catch.hpp>

#include "cmd/trace_server_handle.h"

namespace shk {

TEST_CASE("TraceServerHandle") {
  SECTION("InvalidCommand") {
    auto handle = TraceServerHandle::open("/nonexisting");
    std::string err;
    CHECK(handle->getShkTracePath(&err).empty());
    CHECK(err == "posix_spawn() failed");
  }

  SECTION("WrongAcknowledgementMessage") {
    auto handle = TraceServerHandle::open("/bin/echo");
    std::string err;
    CHECK(handle->getShkTracePath(&err).empty());
    CHECK(err == "did not see expected acknowledgement message");
  }

  SECTION("Success") {
    auto handle = TraceServerHandle::open("shk-trace-dummy");
    std::string err;
    CHECK(!handle->getShkTracePath(&err).empty());
    CHECK(err == "");
  }
}

}  // namespace shk
