#pragma once

#include <string>

#include <servers/bootstrap.h>

#include "mach_port.h"

namespace shk {

enum class MachPortRegistrationResult {
  SUCCESS,
  IN_USE,
  FAILURE
};

enum class MachOpenPortResult {
  SUCCESS,
  FAILURE
};

std::pair<MachReceiveRight, MachPortRegistrationResult> registerNamedPort(
    const std::string &name);

std::pair<MachSendRight, MachOpenPortResult> openNamedPort(
    const std::string &name);

}  // namespace shk
