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

#include <google/bigtable/v2/bigtable.rsgrpc.pb.h>
#include <rs-grpc/client.h>
#include <rs-grpc/server.h>

#include "store_server.h"

namespace shk {

int main(int /*argc*/, const char * /*argv*/[]) {
  setenv("GRPC_VERBOSITY", "DEBUG", /*overwrite:*/0);
  setenv("GRPC_ABORT_ON_LEAKS", "YES", /*overwrite:*/0);

  auto channel = ::grpc::CreateChannel(
      "127.0.0.1:8086", ::grpc::InsecureChannelCredentials());

  auto bigtable_client = std::shared_ptr<google::bigtable::v2::Bigtable>(
      google::bigtable::v2::Bigtable::NewClient(channel));

  std::string server_address = "unix:shk_store_test.socket";

  RsGrpcServer::Builder server_builder;
  server_builder.GrpcServerBuilder()
      .AddListeningPort(server_address, ::grpc::InsecureServerCredentials());
  server_builder.RegisterService(MakeStore(bigtable_client));

  auto server = server_builder.BuildAndStart();

  google::bigtable::v2::MutateRowRequest request;
  request.set_table_name("test_table");
  request.set_row_key("row_key");

  auto &mutation = *request.add_mutations();
  auto &set_cell = *mutation.mutable_set_cell();
  set_cell.set_family_name("family");
  set_cell.set_column_qualifier("col");
  set_cell.set_timestamp_micros(-1);
  set_cell.set_value("val");

  auto sub = bigtable_client
      ->MutateRow(server.CallContext(), std::move(request))
      .Subscribe(MakeSubscriber(
          [](auto &&) {
            printf("ONNEXT\n");
          },
          [](std::exception_ptr error) {
            printf("ERROR: %s\n", ExceptionMessage(error).c_str());
          },
          []() {
            printf("COMPLETE\n");
          }));
  sub.Request(ElementCount::Unbounded());

  printf("\nshk-store listening to %s\n\n", server_address.c_str());
  server.Run();

  return 0;
}

}  // namespace shk

int main(int argc, const char *argv[]) {
  return shk::main(argc, argv);
}
