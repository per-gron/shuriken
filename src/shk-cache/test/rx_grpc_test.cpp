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

auto makeTestRequest(int data) {
  flatbuffers::FlatBufferBuilder fbb;
  auto test_request = CreateTestRequest(fbb, data);
  fbb.Finish(test_request);
  return Flatbuffer<TestRequest>::sharedFromBuilder(&fbb);
}

auto makeTestResponse(int data) {
  flatbuffers::FlatBufferBuilder fbb;
  auto test_response = CreateTestResponse(fbb, data);
  fbb.Finish(test_response);
  return Flatbuffer<TestResponse>::sharedFromBuilder(&fbb);
}

auto doubleHandler(const FlatbufferPtr<TestRequest> &request) {
  return rxcpp::observable<>::just(makeTestResponse((*request)->data() * 2));
}

auto serverStreamHandler(const FlatbufferPtr<TestRequest> &request) {
  return rxcpp::observable<>::range(1, (*request)->data())
      .map(&makeTestResponse);
}

}  // anonymous namespace

TEST_CASE("RxGrpc") {
  auto server_address = "unix:rx_grpc_test.socket";

  RxGrpcServer::Builder server_builder;
  server_builder.grpcServerBuilder()
      .AddListeningPort(server_address, grpc::InsecureServerCredentials());

  server_builder.registerService<TestService::AsyncService>()
      .registerMethod<FlatbufferRefTransform>(
          &TestService::AsyncService::RequestDouble,
          &doubleHandler)
      .registerMethod<FlatbufferRefTransform>(
          &TestService::AsyncService::RequestServerStream,
          &serverStreamHandler);

  RxGrpcClient runloop;

  auto channel = grpc::CreateChannel(
      server_address, grpc::InsecureChannelCredentials());

  auto test_client = runloop.makeClient<FlatbufferRefTransform>(
      TestService::NewStub(channel));

  auto server = server_builder.buildAndStart();
  std::thread server_thread([&] { server.run(); });

  const auto run = [&](const auto &observable) {
    observable
        .subscribe(
            [](auto) {
            },
            [&runloop](std::exception_ptr error) {
              CHECK(!"request should not fail");
              runloop.shutdown();
            },
            [&runloop]() {
              runloop.shutdown();
            });

    runloop.run();
  };

  SECTION("normal call") {
    SECTION("direct") {
      run(test_client
          .invoke(
              &TestService::Stub::AsyncDouble, makeTestRequest(123))
          .map(
              [](const FlatbufferPtr<TestResponse> &response) {
                CHECK((*response)->data() == 123 * 2);
                return "ignored";
              }));
    }

    SECTION("delayed") {
      // This test can break if invoke doesn't take ownership of the request for
      // example.
      auto call = test_client
          .invoke(
              &TestService::Stub::AsyncDouble, makeTestRequest(123))
          .map(
              [](const FlatbufferPtr<TestResponse> &response) {
                CHECK((*response)->data() == 123 * 2);
                return "ignored";
              });
      run(call);
    }

    SECTION("two calls") {
      auto call_a = test_client.invoke(
          &TestService::Stub::AsyncDouble, makeTestRequest(123));
      auto call_b = test_client.invoke(
          &TestService::Stub::AsyncDouble, makeTestRequest(321));
      run(call_a
          .zip(call_b)
          .map(
              [](std::tuple<
                    const FlatbufferPtr<TestResponse>,
                    const FlatbufferPtr<TestResponse>> responses) {
                CHECK((*std::get<0>(responses))->data() == 123 * 2);
                CHECK((*std::get<1>(responses))->data() == 321 * 2);
                return "ignored";
              }));
    }

    SECTION("same call twice") {
      auto call = test_client.invoke(
          &TestService::Stub::AsyncDouble, makeTestRequest(123));
      run(call
          .zip(call)
          .map(
              [](std::tuple<
                    const FlatbufferPtr<TestResponse>,
                    const FlatbufferPtr<TestResponse>> responses) {
                CHECK((*std::get<0>(responses))->data() == 123 * 2);
                CHECK((*std::get<1>(responses))->data() == 123 * 2);
                return "ignored";
              }));
    }
  }

  server.shutdown();
  server_thread.join();
}

}  // namespace shk
