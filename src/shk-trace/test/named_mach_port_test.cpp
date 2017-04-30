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

#include <catch.hpp>

#include <bsm/libbsm.h>
#include <mach/mach.h>
#include <unistd.h>

#include "named_mach_port.h"

namespace shk {
namespace {

struct MachSendMsg {
  mach_msg_header_t header;
  mach_msg_body_t body;
};

struct MachRecvMsg : public MachSendMsg {
  mach_msg_audit_trailer_t trailer;
};

}  // anonymous namespace

TEST_CASE("NamedMachPort") {
  const std::string port_name = "com.pereckerdal.test_name";

  SECTION("OpenMissing") {
    auto result = openNamedPort(port_name).second;
    CHECK(result == MachOpenPortResult::NOT_FOUND);
  }

  SECTION("Register") {
    auto result = registerNamedPort(port_name).second;
    CHECK(result == MachPortRegistrationResult::SUCCESS);
  }

  SECTION("DoubleRegister") {
    auto port = registerNamedPort(port_name);
    auto result = registerNamedPort(port_name).second;
    CHECK(result == MachPortRegistrationResult::IN_USE);
  }

  SECTION("RegisterAndOpen") {
    auto server_port = registerNamedPort(port_name);
    auto result = openNamedPort(port_name).second;
    CHECK(result == MachOpenPortResult::SUCCESS);
  }

  SECTION("OpenAfterClose") {
    {
      auto server_port = registerNamedPort(port_name);
    }
    auto result = openNamedPort(port_name).second;
    CHECK(result == MachOpenPortResult::NOT_FOUND);
  }

  SECTION("ExchangeMessage") {
    auto server = registerNamedPort(port_name);
    auto client = openNamedPort(port_name);
    CHECK(server.second == MachPortRegistrationResult::SUCCESS);
    CHECK(client.second == MachOpenPortResult::SUCCESS);

    MachSendMsg send_msg;
    bzero(&send_msg, sizeof(send_msg));
    send_msg.header.msgh_bits =
        MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
    send_msg.header.msgh_size = sizeof(send_msg);
    send_msg.header.msgh_remote_port = client.first.get();
    send_msg.header.msgh_local_port = MACH_PORT_NULL;
    send_msg.header.msgh_reserved = 0;
    send_msg.header.msgh_id = 0;
    send_msg.body.msgh_descriptor_count = 0;
    CHECK(mach_msg(
        &send_msg.header,
        MACH_SEND_MSG | MACH_SEND_TIMEOUT,
        send_msg.header.msgh_size,
        /* receive limit: */0,
        /* receive name: */MACH_PORT_NULL,
        /* timeout: */0,
        /* notification port: */MACH_PORT_NULL) == KERN_SUCCESS);

    MachRecvMsg recv_msg;
    bzero(&recv_msg, sizeof(recv_msg));
    recv_msg.header.msgh_size = sizeof(recv_msg);
    recv_msg.header.msgh_local_port = server.first.get();
    const mach_msg_option_t options = MACH_RCV_MSG |
        MACH_RCV_TRAILER_TYPE(MACH_RCV_TRAILER_AUDIT) |
        MACH_RCV_TRAILER_ELEMENTS(MACH_RCV_TRAILER_AUDIT);
    CHECK(mach_msg(
        &recv_msg.header,
        options,
        0,
        sizeof(recv_msg),
        server.first.get(),
        MACH_MSG_TIMEOUT_NONE,
        MACH_PORT_NULL) == KERN_SUCCESS);

    pid_t pid = audit_token_to_pid(recv_msg.trailer.msgh_audit);
    CHECK(pid == getpid());
  }
  // Exchange message
}

}  // namespace shk
