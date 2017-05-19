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

#include "rx_grpc.h"
#include "rx_grpc_flatbuffers.h"

#include "rxgrpctest.grpc.fb.h"

using namespace RxGrpcTest;

namespace shk {
namespace {

auto doubleHandler(const FlatbufferPtr<TestRequest> &request) {
  flatbuffers::FlatBufferBuilder response_builder;
  auto test_response = CreateTestResponse(
      response_builder,
      /*data:*/(*request)->data() * 2);

  response_builder.Finish(test_response);

  auto response = Flatbuffer<TestResponse>::sharedFromBuilder(
      &response_builder);

  return rxcpp::observable<>::just(response);
}

auto makeTestRequest(int data) {
  flatbuffers::FlatBufferBuilder fbb;
  auto test_request = CreateTestRequest(fbb, data);
  fbb.Finish(test_request);
  return Flatbuffer<TestRequest>::sharedFromBuilder(&fbb);
}

}  // anonymous namespace

TEST_CASE("RxGrpc") {
  bool postcondition = true;

  auto server_address = "unix:rx_grpc_test.socket";

  RxGrpcServer::Builder server_builder;
  server_builder.grpcServerBuilder()
      .AddListeningPort(server_address, grpc::InsecureServerCredentials());

  server_builder.registerService<TestService::AsyncService>()
      .registerMethod<FlatbufferRefTransform>(
          &TestService::AsyncService::RequestDouble,
          &doubleHandler);

  RxGrpcClient runloop;

  auto channel = grpc::CreateChannel(
      server_address, grpc::InsecureChannelCredentials());

  auto test_client = runloop.makeClient<FlatbufferRefTransform>(
      TestService::NewStub(channel));

  auto server = server_builder.buildAndStart();
  std::thread server_thread([&] { server.run(); });

  SECTION("one non-streaming call") {
    postcondition = false;

    test_client
        .invoke(
            &TestService::Stub::AsyncDouble, makeTestRequest(123))
        .subscribe(
            [&postcondition](const FlatbufferPtr<TestResponse> &response) {
              CHECK((*response)->data() == 123 * 2);
              postcondition = true;  // Ensure that the check actually happened
            },
            [&runloop](std::exception_ptr error) {
              CHECK(!"request should not fail");
              runloop.shutdown();
            },
            [&runloop]() {
              runloop.shutdown();
            });

    runloop.run();
  }

  server.shutdown();
  server_thread.join();

  CHECK(postcondition);
}

}  // namespace shk
