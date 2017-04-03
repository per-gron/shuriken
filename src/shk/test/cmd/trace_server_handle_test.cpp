#include <catch.hpp>

#include "cmd/trace_server_handle.h"

namespace shk {

TEST_CASE("TraceServerHandle") {
  SECTION("Mock") {
    TraceServerHandle();
  }

  SECTION("InvalidCommand") {
    std::string err;
    CHECK(!TraceServerHandle::open("/nonexisting", &err));
    CHECK(err == "did not see expected acknowledgement message");
  }

  SECTION("WrongAcknowledgementMessage") {
    std::string err;
    CHECK(!TraceServerHandle::open("/bin/echo hey", &err));
    CHECK(err == "did not see expected acknowledgement message");
  }

  SECTION("Success") {
    std::string err;
    CHECK(TraceServerHandle::open("/bin/echo serving", &err));
  }
}

}  // namespace shk
