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

#include "named_mach_port.h"

#include <cstdlib>

namespace shk {

std::pair<MachReceiveRight, MachPortRegistrationResult> registerNamedPort(
    const std::string &name) {
  mach_port_t port = MACH_PORT_NULL;
  auto kr = bootstrap_check_in(bootstrap_port, name.c_str(), &port);

  bool in_use =
      kr == BOOTSTRAP_SERVICE_ACTIVE || kr == BOOTSTRAP_NOT_PRIVILEGED;
  auto result =
      kr == BOOTSTRAP_SUCCESS ? MachPortRegistrationResult::SUCCESS :
      in_use ? MachPortRegistrationResult::IN_USE :
      MachPortRegistrationResult::FAILURE;

  return std::make_pair(MachReceiveRight(port), result);
}

std::pair<MachSendRight, MachOpenPortResult> openNamedPort(
    const std::string &name) {
  mach_port_t port = MACH_PORT_NULL;
  kern_return_t kr = bootstrap_look_up(bootstrap_port, name.c_str(), &port);

  auto result =
    kr == BOOTSTRAP_SUCCESS ? MachOpenPortResult::SUCCESS :
    kr == BOOTSTRAP_UNKNOWN_SERVICE ? MachOpenPortResult::NOT_FOUND :
    MachOpenPortResult::FAILURE;

  return std::make_pair(MachSendRight(port), result);
}

}  // namespace shk
