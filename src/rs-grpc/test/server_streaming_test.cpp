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

#include <flatbuffers/grpc.h>
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

#include "rsgrpctest.grpc.fb.h"
#include "test_util.h"

using namespace RsGrpcTest;

namespace shk {
namespace {

auto RepeatHandler(Flatbuffer<TestRequest> request) {
  int count = request->data();
  return Pipe(
      Range(1, count),
      Map(&MakeTestResponse));
}

auto RepeatThenFailHandler(Flatbuffer<TestRequest> request) {
  return Concat(
      RepeatHandler(std::move(request)),
      Throw(std::runtime_error("repeat_fail")));
}

auto ServerStreamHangHandler(Flatbuffer<TestRequest> request) {
  return Never();
}

auto InfiniteRepeatHandler(Flatbuffer<TestRequest> request) {
  // If client-side rs-grpc violates backpressure requirements by requesting
  // an unbounded number of elements from this infinite stream, then this will
  // smash the stack or run out of memory.
  return MakeInfiniteResponse();
}

auto ServerStreamBackpressureViolationHandler(Flatbuffer<TestRequest> request) {
  return MakePublisher([](auto &&subscriber) {
    // Emit element before it was asked for: streams should not do
    // this.
    subscriber.OnNext(MakeTestResponse(1));
    subscriber.OnNext(MakeTestResponse(2));
    subscriber.OnNext(MakeTestResponse(3));
    return MakeSubscription();
  });
}

}  // anonymous namespace

TEST_CASE("Server streaming RPC") {
  auto server_address = "unix:rs_grpc_test.socket";

  RsGrpcServer::Builder server_builder;
  server_builder.GrpcServerBuilder()
      .AddListeningPort(server_address, grpc::InsecureServerCredentials());

  std::atomic<int> hang_on_seen_elements(0);

  server_builder.RegisterService<TestService::AsyncService>()
      .RegisterMethod(
          &TestService::AsyncService::RequestRepeat,
          &RepeatHandler)
      .RegisterMethod(
          &TestService::AsyncService::RequestRepeatThenFail,
          &RepeatThenFailHandler)
      .RegisterMethod(
          &TestService::AsyncService::RequestServerStreamHang,
          &ServerStreamHangHandler)
      .RegisterMethod(
          &TestService::AsyncService::RequestInfiniteRepeat,
          &InfiniteRepeatHandler)
      .RegisterMethod(
          &TestService::AsyncService::RequestServerStreamBackpressureViolation,
          &ServerStreamBackpressureViolationHandler);

  RsGrpcClient runloop;

  auto channel = grpc::CreateChannel(
      server_address, grpc::InsecureChannelCredentials());

  auto test_client = runloop.MakeClient(
      TestService::NewStub(channel));

  auto server = server_builder.BuildAndStart();
  std::thread server_thread([&] { server.Run(); });

  SECTION("no responses") {
    Run(&runloop, Pipe(
        test_client.Invoke(
            &TestService::Stub::AsyncRepeat, MakeTestRequest(0)),
        Map([](Flatbuffer<TestResponse> &&response) {
          // Should never be called; this should be a stream that ends
          // without any values
          CHECK(false);
          return "ignored";
        })));
  }

  SECTION("cancellation") {
    SECTION("from client side") {
      SECTION("after Request") {
        auto call = test_client.Invoke(
            &TestService::Stub::AsyncServerStreamHang,
            MakeTestRequest(0));

        bool cancelled = false;
        auto subscription = call
            .Subscribe(MakeSubscriber(
                [](auto &&) {
                  CHECK(!"OnNext should not be called");
                },
                [&cancelled](std::exception_ptr error) {
                  CHECK(ExceptionMessage(error) == "Cancelled");
                  cancelled = true;
                },
                [] {
                  CHECK(!"OnComplete should not be called");
                }));
        subscription.Request(ElementCount::Unbounded());
        subscription.Cancel();

        // The cancelled request will take two runloop iterations to actually
        // happen.
        CHECK(runloop.Next());
        CHECK(runloop.Next());

        CHECK(!cancelled);

        ShutdownAllowOutstandingCall(&server);

        runloop.Shutdown();
        runloop.Run();
      }

      SECTION("before Request") {
        auto call = test_client.Invoke(
            &TestService::Stub::AsyncRepeat,
            MakeTestRequest(1));

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

  SECTION("backpressure") {
    int latest_seen_response = 0;
    auto publisher = Pipe(
        test_client.Invoke(
            &TestService::Stub::AsyncRepeat, MakeTestRequest(10)),
        Map([&latest_seen_response](Flatbuffer<TestResponse> response) {
          CHECK(++latest_seen_response == response->data());
          return "ignored";
        }));

    SECTION("call Invoke, request only some elements") {
      for (int i = 0; i < 4; i++) {
        latest_seen_response = 0;
        RunExpectTimeout(&runloop, publisher, ElementCount(i));
        CHECK(latest_seen_response == i);
      }
    }

    SECTION("Request one element at a time") {
      Subscription subscription = Subscription(publisher
          .Subscribe(MakeSubscriber(
              [&subscription](auto &&) {
                subscription.Request(ElementCount(1));
              },
              [](std::exception_ptr error) {
                CHECK(!"request should not fail");
              },
              [&runloop]() {
                runloop.Shutdown();
              })));

      subscription.Request(ElementCount(1));
      runloop.Run();
      CHECK(latest_seen_response == 10);
    }

    SECTION("Request after stream end") {
      Subscription subscription = Subscription(publisher
          .Subscribe(MakeSubscriber(
              [&subscription](auto &&) {
                // Ignore
              },
              [](std::exception_ptr error) {
                CHECK(!"request should not fail");
              },
              [&runloop]() {
                runloop.Shutdown();
              })));

      subscription.Request(ElementCount::Unbounded());
      runloop.Run();

      subscription.Request(ElementCount(0));
      subscription.Request(ElementCount(1));
      subscription.Request(ElementCount(2));
      subscription.Request(ElementCount::Unbounded());
    }

    SECTION("Request one element from infinite stream") {
      auto request = test_client.Invoke(
          &TestService::Stub::AsyncInfiniteRepeat, MakeTestRequest(0));

      auto subscription = request.Subscribe(MakeSubscriber());
      subscription.Request(ElementCount(1));

      CHECK(runloop.Next());
      CHECK(runloop.Next());

      ShutdownAllowOutstandingCall(&server);
    }

    SECTION("violate backpressure in provided publisher (server side)") {
      auto publisher = Pipe(
          test_client.Invoke(
              &TestService::Stub::AsyncServerStreamBackpressureViolation,
              MakeTestRequest(0)));
      auto error = RunExpectError(&runloop, publisher);
      CHECK(ExceptionMessage(error) == "Backpressure violation");
    }
  }

  SECTION("one response") {
    Run(&runloop, Pipe(
        test_client
            .Invoke(&TestService::Stub::AsyncRepeat, MakeTestRequest(1)),
        Map([](Flatbuffer<TestResponse> response) {
          CHECK(response->data() == 1);
          return "ignored";
        }),
        Count(),
        Map([](int count) {
          CHECK(count == 1);
          return "ignored";
        })));
  }

  SECTION("two responses") {
    auto responses = test_client.Invoke(
        &TestService::Stub::AsyncRepeat, MakeTestRequest(2));

    auto check_count = Pipe(
        responses,
        Count(),
        Map([](int count) {
          CHECK(count == 2);
          return "ignored";
        }));

    auto check_sum = Pipe(
        responses,
        Map([](Flatbuffer<TestResponse> response) {
          return response->data();
        }),
        Sum(),
        Map([](int sum) {
          CHECK(sum == 3);
          return "ignored";
        }));

    Run(&runloop, Merge<const char *>(check_count, check_sum));
  }

  SECTION("no responses then fail") {
    auto error = RunExpectError(&runloop, Pipe(
        test_client.Invoke(
            &TestService::Stub::AsyncRepeatThenFail, MakeTestRequest(0)),
        Map([](Flatbuffer<TestResponse> response) {
          CHECK(!"should not happen");
          return "unused";
        })));
    CHECK(ExceptionMessage(error) == "repeat_fail");
  }

  SECTION("one response then fail") {
    int count = 0;
    auto error = RunExpectError(&runloop, Pipe(
        test_client.Invoke(
            &TestService::Stub::AsyncRepeatThenFail, MakeTestRequest(1)),
        Map([&count](Flatbuffer<TestResponse> response) {
          count++;
          return "unused";
        })));
    CHECK(ExceptionMessage(error) == "repeat_fail");
    CHECK(count == 1);
  }

  SECTION("two responses then fail") {
    int count = 0;
    auto error = RunExpectError(&runloop, Pipe(
        test_client.Invoke(
            &TestService::Stub::AsyncRepeatThenFail, MakeTestRequest(2)),
        Map([&count](Flatbuffer<TestResponse> response) {
          count++;
          return "unused";
        })));
    CHECK(ExceptionMessage(error) == "repeat_fail");
    CHECK(count == 2);
  }

  SECTION("two calls") {
    auto responses_1 = Pipe(
        test_client.Invoke(
            &TestService::Stub::AsyncRepeat, MakeTestRequest(2)),
        Map([](Flatbuffer<TestResponse> response) {
          return response->data();
        }),
        Sum(),
        Map([](int sum) {
          CHECK(sum == 3);
          return "ignored";
        }));

    auto responses_2 = Pipe(
        test_client.Invoke(
            &TestService::Stub::AsyncRepeat, MakeTestRequest(3)),
        Map([](Flatbuffer<TestResponse> response) {
          return response->data();
        }),
        Sum(),
        Map([](int sum) {
          CHECK(sum == 6);
          return "ignored";
        }));

    Run(&runloop, Merge<const char *>(responses_1, responses_2));
  }

  {
    using namespace std::chrono_literals;
    auto deadline = std::chrono::system_clock::now() + 1000h;
    server.Shutdown(deadline);
  }
  server_thread.join();
}

}  // namespace shk
