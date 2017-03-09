#include "mach_port.h"

#include <cstdlib>

namespace shk {
namespace detail {

void deallocatePort(mach_port_t port) {
  auto kr = mach_port_deallocate(mach_task_self(), port);
  if (kr != KERN_SUCCESS) {
    // We have no access to logging what happened :-(
    abort();
  }
}

void derefReceiveRight(mach_port_t port) {
  auto kr = mach_port_mod_refs(
      mach_task_self(), port, MACH_PORT_RIGHT_RECEIVE, -1);
  if (kr != KERN_SUCCESS) {
    // We have no access to logging what happened :-(
    abort();
  }
}

}  // namespace detail

std::pair<MachReceiveRight, MachSendRight> makePortPair() {
  mach_port_t raw_receive_port;
  if (mach_port_allocate(
          mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &raw_receive_port)) {
    return std::make_pair(
        MachReceiveRight(MACH_PORT_NULL),
        MachSendRight(MACH_PORT_NULL));
  }
  MachReceiveRight receive_port(raw_receive_port);

  mach_port_t raw_send_port;
  mach_msg_type_name_t send_port_type;
  if (mach_port_extract_right(
          mach_task_self(),
          receive_port.get(),
          MACH_MSG_TYPE_MAKE_SEND,
          &raw_send_port,
          &send_port_type)) {
    return std::make_pair(
        MachReceiveRight(MACH_PORT_NULL),
        MachSendRight(MACH_PORT_NULL));
  }
  MachSendRight send_port(raw_send_port);

  return std::make_pair(
      std::move(receive_port),
      std::move(send_port));
}

}  // namespace shk
