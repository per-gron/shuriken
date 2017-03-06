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
}  // namespace shk
