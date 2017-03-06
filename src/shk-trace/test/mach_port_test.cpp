#include <catch.hpp>

#include "mach_port.h"

namespace shk {

TEST_CASE("MachPort") {
  SECTION("SendDestroyDefault") {
    MachSendRight right(MACH_PORT_NULL);
  }

  SECTION("ReceiveDestroyDefault") {
    MachReceiveRight right(MACH_PORT_NULL);
  }
}

}  // namespace shk
