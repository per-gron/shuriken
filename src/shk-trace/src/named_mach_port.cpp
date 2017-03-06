#include "named_mach_port.h"

#include <cstdlib>

namespace shk {
namespace {

mach_port_t getBootstrapPort() {
  mach_port_t bootstrap_port;
  auto kr = task_get_bootstrap_port(mach_task_self(), &bootstrap_port);
  if (kr != KERN_SUCCESS) {
    fprintf(stderr, "task_get_bootstrap_port(): %s\n", mach_error_string(kr));
    abort();
  }
  return bootstrap_port;
}

}  // anonymous namespace

std::pair<MachReceiveRight, MachPortRegistrationResult> registerNamedPort(
    const std::string &name) {
  mach_port_t port = MACH_PORT_NULL;
  auto kr = bootstrap_check_in(bootstrap_port, name.c_str(), &port);

  bool in_use =
      kr == BOOTSTRAP_SERVICE_ACTIVE || kr == BOOTSTRAP_NOT_PRIVILEGED;
  auto result =
      kr == KERN_SUCCESS ? MachPortRegistrationResult::SUCCESS :
      in_use ? MachPortRegistrationResult::IN_USE :
      MachPortRegistrationResult::FAILURE;

  return std::make_pair(MachReceiveRight(port), result);
}

std::pair<MachSendRight, MachOpenPortResult> openNamedPort(
    const std::string &name) {
  mach_port_t port = MACH_PORT_NULL;
  kern_return_t kr = bootstrap_look_up(bootstrap_port, name.c_str(), &port);

  auto result = kr == KERN_SUCCESS ?
      MachOpenPortResult::SUCCESS : MachOpenPortResult::FAILURE;

  return std::make_pair(MachSendRight(port), result);
}

}  // namespace shk
