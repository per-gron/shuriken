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

#include <stdio.h>
#include <string>

#include <rs-grpc/server.h>

namespace shk {

int main(int /*argc*/, const char * /*argv*/[]) {
  setenv("GRPC_VERBOSITY", "DEBUG", /*overwrite:*/0);
  setenv("GRPC_ABORT_ON_LEAKS", "YES", /*overwrite:*/0);

  std::string server_address = "unix:shk_store_test.socket";

  RsGrpcServer::Builder server_builder;
  server_builder.GrpcServerBuilder()
      .AddListeningPort(server_address, grpc::InsecureServerCredentials());

  // TODO(peck): Register service

  auto channel = grpc::CreateChannel(
      server_address, grpc::InsecureChannelCredentials());

  auto server = server_builder.BuildAndStart();

  printf("\nshk-store listening to %s\n\n", server_address.c_str());
  server.Run();

  return 0;
}

}  // namespace shk

int main(int argc, const char *argv[]) {
  return shk::main(argc, argv);
}
