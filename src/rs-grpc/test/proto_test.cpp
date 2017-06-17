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

#include <atomic>
#include <chrono>
#include <thread>

#include <rs/concat.h>
#include <rs/count.h>
#include <rs/empty.h>
#include <rs/just.h>
#include <rs/map.h>
#include <rs/merge.h>
#include <rs/never.h>
#include <rs/pipe.h>
#include <rs/range.h>
#include <rs/repeat.h>
#include <rs/scan.h>
#include <rs/splat.h>
#include <rs/sum.h>
#include <rs/throw.h>
#include <rs/zip.h>
#include <rs-grpc/server.h>

#include "rsgrpctest.grpc.pb.h"
#include "test_util.h"

using namespace RsGrpcTest;

namespace shk {
namespace {

auto DoubleHandler(TestRequest &&request) {
  TestResponse response;
  response.set_data(request.data() * 2);
  return Just(response);
}

}  // anonymous namespace

TEST_CASE("rs-grpc with Protobuf") {
  auto server_address = "unix:rs_grpc_test.socket";

  RsGrpcServer::Builder server_builder;
  server_builder.GrpcServerBuilder()
      .AddListeningPort(server_address, grpc::InsecureServerCredentials());

  std::atomic<int> hang_on_seen_elements(0);

  server_builder.RegisterService<TestService::AsyncService>()
      .RegisterMethod(
          &TestService::AsyncService::RequestDouble,
          &DoubleHandler);

  RsGrpcClient runloop;

  auto channel = grpc::CreateChannel(
      server_address, grpc::InsecureChannelCredentials());

  auto test_client = runloop.MakeClient(
      TestService::NewStub(channel));

  auto server = server_builder.BuildAndStart();
  std::thread server_thread([&] { server.Run(); });

  SECTION("direct") {
    TestRequest request;
    request.set_data(123);

    Run(&runloop, Pipe(
        test_client.Invoke(
            &TestService::Stub::AsyncDouble, request),
        Map([](TestResponse &&response) {
          CHECK(response.data() == 123 * 2);
          return "ignored";
        })));
  }

  {
    using namespace std::chrono_literals;
    auto deadline = std::chrono::system_clock::now() + 1000h;
    server.Shutdown(deadline);
  }
  server_thread.join();
}

}  // namespace shk
