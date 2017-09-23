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

namespace shk {
namespace {

auto DoubleHandler(const CallContext &ctx, TestRequest &&request) {
  return Just(MakeTestResponse(request.data() * 2));
}

auto UnaryFailHandler(const CallContext &ctx, TestRequest &&request) {
  return Throw(std::runtime_error("unary_fail"));
}

auto UnaryNoResponseHandler(const CallContext &ctx, TestRequest &&request) {
  return Empty();
}

auto UnaryTwoResponsesHandler(const CallContext &ctx, TestRequest &&request) {
  return Just(
      MakeTestResponse(1),
      MakeTestResponse(2));
}

auto UnaryHangHandler(const CallContext &ctx, TestRequest &&request) {
  return Never();
}

}  // anonymous namespace

TEST_CASE("Unary RPC") {
  InitTests();

  // TODO(peck): Add support for server-side cancellation
  // TODO(peck): Add support for timeouts
  // TODO(peck): Test finishing bidi and unidirectional streams in different orders

  auto server_address = "unix:rs_grpc_test.socket";

  RsGrpcServer::Builder server_builder;
  server_builder.GrpcServerBuilder()
      .AddListeningPort(server_address, grpc::InsecureServerCredentials());

  std::atomic<int> hang_on_seen_elements(0);

  server_builder.RegisterService<TestService::AsyncService>()
      .RegisterMethod(
          &TestService::AsyncService::RequestDouble,
          &DoubleHandler)
      .RegisterMethod(
          &TestService::AsyncService::RequestUnaryFail,
          &UnaryFailHandler)
      .RegisterMethod(
          &TestService::AsyncService::RequestUnaryNoResponse,
          &UnaryNoResponseHandler)
      .RegisterMethod(
          &TestService::AsyncService::RequestUnaryTwoResponses,
          &UnaryTwoResponsesHandler)
      .RegisterMethod(
          &TestService::AsyncService::RequestUnaryHang,
          &UnaryHangHandler);

  RsGrpcClientRunloop runloop;
  CallContext ctx = runloop.CallContext();

  auto channel = grpc::CreateChannel(
      server_address, grpc::InsecureChannelCredentials());

  auto test_client = MakeRsGrpcClient(TestService::NewStub(channel));

  auto server = server_builder.BuildAndStart();
  std::thread server_thread([&] { server.Run(); });

  // TODO(peck): Test what happens when calling unimplemented endpoint. I think
  // right now it just waits forever, which is not nice at all.

  SECTION("direct") {
    Run(&runloop, Pipe(
        test_client.Invoke(
            ctx, &TestService::Stub::AsyncDouble, MakeTestRequest(123)),
        Map([](TestResponse &&response) {
          CHECK(response.data() == 123 * 2);
          return "ignored";
        })));
  }

  SECTION("backpressure") {
    SECTION("call Invoke but don't request a value") {
      auto publisher = Pipe(
          test_client.Invoke(
              ctx, &TestService::Stub::AsyncDouble, MakeTestRequest(123)),
          Map([](TestResponse &&response) {
            CHECK(!"should not be invoked");
            return "ignored";
          }));
      std::shared_ptr<void> tag = RunExpectTimeout(&runloop, publisher);
    }
  }

  SECTION("Request twice") {
    auto request = Pipe(
        test_client.Invoke(
            ctx, &TestService::Stub::AsyncDouble, MakeTestRequest(123)),
        Map([](TestResponse &&response) {
          CHECK(response.data() == 123 * 2);
          return "ignored";
        }));
    Run(&runloop, request, [](AnySubscription &&sub) {
      sub.Request(ElementCount(1));
      sub.Request(ElementCount(1));
    });
  }

  SECTION("failed RPC") {
    auto error = RunExpectError(&runloop, Pipe(
        test_client.Invoke(
            ctx, &TestService::Stub::AsyncUnaryFail, MakeTestRequest(0)),
        Map([](TestResponse &&response) {
          CHECK(!"should not happen");
          return "unused";
        })));
    CHECK(ExceptionMessage(error) == "unary_fail");
  }

  SECTION("failed RPC because of no response") {
    auto error = RunExpectError(&runloop, Pipe(
        test_client.Invoke(
            ctx, &TestService::Stub::AsyncUnaryNoResponse, MakeTestRequest(0)),
        Map([](TestResponse &&response) {
          CHECK(!"should not happen");
          return "unused";
        })));
    CHECK(ExceptionMessage(error) == "No response");
  }

  SECTION("failed RPC because of two responses") {
    auto error = RunExpectError(&runloop, Pipe(
        test_client.Invoke(
            ctx,
            &TestService::Stub::AsyncUnaryTwoResponses,
            MakeTestRequest(0)),
        Map([](TestResponse &&response) {
          CHECK(!"should not happen");
          return "unused";
        })));
    CHECK(ExceptionMessage(error) == "Too many responses");
  }

  SECTION("RPC that never completes") {
    auto call = test_client.Invoke(
        ctx,
        &TestService::Stub::AsyncUnaryHang,
        MakeTestRequest(0));

    auto error = RunExpectError(&runloop, call);
    CHECK(ExceptionMessage(error) == "Cancelled");
  }

  SECTION("cancellation") {
    SECTION("from client side") {
      SECTION("after Request") {
        auto call = test_client.Invoke(
            ctx,
            &TestService::Stub::AsyncUnaryHang,
            MakeTestRequest(0));

        auto subscription = call
            .Subscribe(MakeSubscriber(
                [](auto &&) {
                  CHECK(!"OnNext should not be called");
                },
                [](std::exception_ptr error) {
                  CHECK(!"OnError should not be called");
                  printf(
                      "Got exception: %s\n", ExceptionMessage(error).c_str());
                },
                []() {
                  CHECK(!"OnComplete should not be called");
                }));
        subscription.Request(ElementCount::Unbounded());
        subscription.Cancel();

        // There is only one thing on the runloop: The cancelled request.
        CHECK(runloop.Next());
      }

      SECTION("before Request") {
        auto call = test_client.Invoke(
            ctx,
            &TestService::Stub::AsyncDouble,
            MakeTestRequest(0));

        auto subscription = call
            .Subscribe(MakeSubscriber(
                [](auto &&) {
                  CHECK(!"OnNext should not be called");
                },
                [](std::exception_ptr error) {
                  CHECK(!"OnError should not be called");
                  printf(
                      "Got exception: %s\n", ExceptionMessage(error).c_str());
                },
                []() {
                  CHECK(!"OnComplete should not be called");
                }));
        subscription.Cancel();
        subscription.Request(ElementCount::Unbounded());

        // There should be nothing on the runloop
        using namespace std::chrono_literals;
        auto deadline = std::chrono::system_clock::now() + 20ms;
        CHECK(runloop.Next(deadline) == grpc::CompletionQueue::TIMEOUT);
      }
    }
  }

  SECTION("delayed") {
    // This test can break if invoke doesn't take ownership of the request for
    // example.
    auto call = Pipe(
        test_client.Invoke(
            ctx, &TestService::Stub::AsyncDouble, MakeTestRequest(123)),
        Map([](TestResponse &&response) {
          CHECK(response.data() == 123 * 2);
          return "ignored";
        }));
    Run(&runloop, call);
  }

  SECTION("two calls") {
    auto call_a = test_client.Invoke(
        ctx, &TestService::Stub::AsyncDouble, MakeTestRequest(123));
    auto call_b = test_client.Invoke(
        ctx, &TestService::Stub::AsyncDouble, MakeTestRequest(321));
    Run(&runloop, Pipe(
        Zip<std::tuple<TestResponse, TestResponse>>(call_a, call_b),
        Map(Splat([](TestResponse &&a, TestResponse &&b) {
          CHECK(a.data() == 123 * 2);
          CHECK(b.data() == 321 * 2);
          return "ignored";
        }))));
  }

  SECTION("same call twice") {
    auto call = test_client.Invoke(
        ctx, &TestService::Stub::AsyncDouble, MakeTestRequest(123));
    Run(&runloop, Pipe(
        Zip<std::tuple<TestResponse, TestResponse>>(call, call),
        Map(Splat([](TestResponse &&a, TestResponse &&b) {
          CHECK(a.data() == 123 * 2);
          CHECK(b.data() == 123 * 2);
          return "ignored";
        }))));
  }

  {
    using namespace std::chrono_literals;
    auto deadline = std::chrono::system_clock::now() + 1000h;
    server.Shutdown(deadline);
  }
  server_thread.join();
}

}  // namespace shk
