#pragma once

#include <mach/mach.h>

#include <util/raii_helper.h>

namespace shk {
namespace detail {

void deallocatePort(mach_port_t port);
void derefReceiveRight(mach_port_t port);

}  // namespace detail

using MachSendRight = RAIIHelper<
    mach_port_t, void, detail::deallocatePort, MACH_PORT_NULL>;

using MachReceiveRight = RAIIHelper<
    mach_port_t, void, detail::derefReceiveRight, MACH_PORT_NULL>;

}  // namespace shk
