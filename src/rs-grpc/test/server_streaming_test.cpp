
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
#include <condition_variable>
#include <mutex>
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

#include "rs-grpc/test/rsgrpctest.rsgrpc.pb.h"
#include "test_util.h"

namespace shk {
namespace {

class AsyncResponder {
 public:
  void SetCallback(const std::function<void ()> &callback) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      CHECK(!callback_);
      callback_ = callback;
    }
    cv_.notify_one();
  }

  void Respond() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !!callback_; });
    callback_();
  }

  explicit operator bool() const {
    return !!callback_;
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::function<void ()> callback_;
};

class ServerStreamingTestServer : public ServerStreamingTest {
 public:
  explicit ServerStreamingTestServer(AsyncResponder *async_responder)
      : async_responder_(*async_responder) {}

  AnyPublisher</*stream*/ TestResponse> Repeat(
      const CallContext &ctx, TestRequest &&request) override {
    int count = request.data();
    return AnyPublisher<TestResponse>(Pipe(
        Range(1, count),
        Map(&MakeTestResponse)));
  }

  AnyPublisher</*stream*/ TestResponse> RepeatThenFail(
      const CallContext &ctx, TestRequest &&request) override {
    return AnyPublisher<TestResponse>(Concat(
        Repeat(ctx, std::move(request)),
        Throw(std::runtime_error("repeat_fail"))));
  }

  AnyPublisher</*stream*/ TestResponse> ServerStreamHang(
      const CallContext &ctx, TestRequest &&request) override {
    return AnyPublisher<TestResponse>(Never());
  }

  AnyPublisher</*stream*/ TestResponse> InfiniteRepeat(
      const CallContext &ctx, TestRequest &&request) override {
    // If client-side rs-grpc violates backpressure requirements by requesting
    // an unbounded number of elements from this infinite stream, then this will
    // smash the stack or run out of memory.
    return AnyPublisher<TestResponse>(MakeInfiniteResponse());
  }

  AnyPublisher</*stream*/ TestResponse> ServerStreamBackpressureViolation(
      const CallContext &ctx, TestRequest &&request) override {
    return AnyPublisher<TestResponse>(MakePublisher([](auto &&subscriber) {
      // Emit element before it was asked for: streams should not do
      // this.
      subscriber.OnNext(MakeTestResponse(1));
      subscriber.OnNext(MakeTestResponse(2));
      subscriber.OnNext(MakeTestResponse(3));
      return MakeSubscription();
    }));
  }

  AnyPublisher</*stream*/ TestResponse> ServerStreamAsyncResponse(
      const CallContext &ctx, TestRequest &&request) override {
    return AnyPublisher<TestResponse>(MakePublisher([this](auto subscriber) {
      auto shared_sub = std::make_shared<decltype(subscriber)>(
          decltype(subscriber)(std::move(subscriber)));

      async_responder_.SetCallback([shared_sub] {
        shared_sub->OnNext(MakeTestResponse(1));
        shared_sub->OnComplete();
      });

      return MakeSubscription();
    }));
  }

 private:
  AsyncResponder &async_responder_;
};

}  // anonymous namespace

TEST_CASE("Server streaming RPC") {
  InitTests();

  auto server_address = "unix:rs_grpc_test.socket";

  RsGrpcServer::Builder server_builder;
  server_builder.GrpcServerBuilder()
      .AddListeningPort(server_address, ::grpc::InsecureServerCredentials());

  AsyncResponder async_responder;

  server_builder
      .RegisterService(
          std::unique_ptr<ServerStreamingTestServer>(
              new ServerStreamingTestServer(&async_responder)))
      .RegisterMethod(
          &grpc::ServerStreamingTest::AsyncService::RequestRepeat,
          &ServerStreamingTestServer::Repeat)
      .RegisterMethod(
          &grpc::ServerStreamingTest::AsyncService::RequestRepeatThenFail,
          &ServerStreamingTestServer::RepeatThenFail)
      .RegisterMethod(
          &grpc::ServerStreamingTest::AsyncService::RequestServerStreamHang,
          &ServerStreamingTestServer::ServerStreamHang)
      .RegisterMethod(
          &grpc::ServerStreamingTest::AsyncService::RequestInfiniteRepeat,
          &ServerStreamingTestServer::InfiniteRepeat)
      .RegisterMethod(
          &grpc::ServerStreamingTest::AsyncService::
              RequestServerStreamBackpressureViolation,
          &ServerStreamingTestServer::ServerStreamBackpressureViolation)
      .RegisterMethod(
          &grpc::ServerStreamingTest::AsyncService::
              RequestServerStreamAsyncResponse,
          &ServerStreamingTestServer::ServerStreamAsyncResponse);

  RsGrpcClientRunloop runloop;
  CallContext ctx = runloop.CallContext();

  auto channel = ::grpc::CreateChannel(
      server_address, ::grpc::InsecureChannelCredentials());

  auto test_client = ServerStreamingTest::NewClient(channel);

  auto server = server_builder.BuildAndStart();
  std::thread server_thread([&] { server.Run(); });

  SECTION("no responses") {
    Run(&runloop, Pipe(
        test_client->Repeat(ctx, MakeTestRequest(0)),
        Map([](TestResponse &&response) {
          // Should never be called; this should be a stream that ends
          // without any values
          CHECK(false);
          return "ignored";
        })));
  }

  SECTION("cancellation") {
    SECTION("from client side") {
#if 0  // TODO(peck): This test is racy, it sometimes leaks memory
      SECTION("after Request") {
        auto call = test_client->ServerStreamHang(ctx, MakeTestRequest(0));

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
        CHECK(runloop.Next());
        CHECK(cancelled);

        runloop.Shutdown();
        runloop.Run();
      }
#endif

      SECTION("before Request") {
        auto call = test_client->Repeat(ctx, MakeTestRequest(1));

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
        CHECK(runloop.Next(deadline) == ::grpc::CompletionQueue::TIMEOUT);
      }
    }
  }

  SECTION("backpressure") {
    int latest_seen_response = 0;
    auto publisher = Pipe(
        test_client->Repeat(ctx, MakeTestRequest(10)),
        Map([&latest_seen_response](TestResponse &&response) {
          CHECK(++latest_seen_response == response.data());
          return "ignored";
        }));

    SECTION("call Invoke, request only some elements") {
      auto invoke_request_n = [&](ElementCount n) {
        {
          std::shared_ptr<void> tag =
              RunExpectTimeout(&runloop, publisher, n);
          CHECK(latest_seen_response == n.Get());

          ShutdownAllowOutstandingCall(&server);
        }

        CHECK(latest_seen_response == n.Get());
      };

      SECTION("0") {
        invoke_request_n(ElementCount(0));
      }

      SECTION("1") {
        invoke_request_n(ElementCount(1));
      }

      SECTION("2") {
        invoke_request_n(ElementCount(2));
      }

      SECTION("3") {
        invoke_request_n(ElementCount(3));
      }
    }

    SECTION("Request one element at a time") {
      AnySubscription subscription = AnySubscription(publisher
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
      AnySubscription subscription = AnySubscription(publisher
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
      auto request = test_client->InfiniteRepeat(ctx, MakeTestRequest(0));

      auto subscription = request.Subscribe(MakeSubscriber());
      subscription.Request(ElementCount(1));

      CHECK(runloop.Next());
      CHECK(runloop.Next());

      ShutdownAllowOutstandingCall(&server);
    }

    SECTION("violate backpressure in provided publisher (server side)") {
      auto publisher = Pipe(
          test_client->ServerStreamBackpressureViolation(
              ctx, MakeTestRequest(0)));
      auto error = RunExpectError(&runloop, publisher);
      CHECK(ExceptionMessage(error) == "Backpressure violation");
    }
  }

  SECTION("one response") {
    Run(&runloop, Pipe(
        test_client->Repeat(ctx, MakeTestRequest(1)),
        Map([](TestResponse &&response) {
          CHECK(response.data() == 1);
          return "ignored";
        }),
        Count(),
        Map([](int count) {
          CHECK(count == 1);
          return "ignored";
        })));
  }

  SECTION("two responses") {
    auto responses = test_client->Repeat(ctx, MakeTestRequest(2));

    auto check_count = Pipe(
        responses,
        Count(),
        Map([](int count) {
          CHECK(count == 2);
          return "ignored";
        }));

    auto check_sum = Pipe(
        responses,
        Map([](TestResponse &&response) {
          return response.data();
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
        test_client->RepeatThenFail(ctx, MakeTestRequest(0)),
        Map([](TestResponse &&response) {
          CHECK(!"should not happen");
          return "unused";
        })));
    CHECK(ExceptionMessage(error) == "repeat_fail");
  }

  SECTION("one response then fail") {
    int count = 0;
    auto error = RunExpectError(&runloop, Pipe(
        test_client->RepeatThenFail(ctx, MakeTestRequest(1)),
        Map([&count](TestResponse &&response) {
          count++;
          return "unused";
        })));
    CHECK(ExceptionMessage(error) == "repeat_fail");
    CHECK(count == 1);
  }

  SECTION("two responses then fail") {
    int count = 0;
    auto error = RunExpectError(&runloop, Pipe(
        test_client->RepeatThenFail(ctx, MakeTestRequest(2)),
        Map([&count](TestResponse &&response) {
          count++;
          return "unused";
        })));
    CHECK(ExceptionMessage(error) == "repeat_fail");
    CHECK(count == 2);
  }

  SECTION("two calls") {
    auto responses_1 = Pipe(
        test_client->Repeat(ctx, MakeTestRequest(2)),
        Map([](TestResponse &&response) {
          return response.data();
        }),
        Sum(),
        Map([](int sum) {
          CHECK(sum == 3);
          return "ignored";
        }));

    auto responses_2 = Pipe(
        test_client->Repeat(ctx, MakeTestRequest(3)),
        Map([](TestResponse &&response) {
          return response.data();
        }),
        Sum(),
        Map([](int sum) {
          CHECK(sum == 6);
          return "ignored";
        }));

    Run(&runloop, Merge<const char *>(responses_1, responses_2));
  }

  SECTION("asynchronous response") {
    auto stream = Pipe(
        test_client->ServerStreamAsyncResponse(ctx, MakeTestRequest(1)),
        Map([](TestResponse &&response) {
          CHECK(response.data() == 1);
          return "ignored";
        }),
        Count(),
        Map([](int count) {
          CHECK(count == 1);
          return "ignored";
        }));

    auto sub = stream.Subscribe(MakeSubscriber(
        [](auto &&) {
          // Ignore OnNext
        },
        [&runloop](std::exception_ptr error) {
          runloop.Shutdown();
          CHECK(!"request should not fail");
          printf("Got exception: %s\n", ExceptionMessage(error).c_str());
        },
        [&runloop]() {
          runloop.Shutdown();
        }));
    sub.Request(ElementCount::Unbounded());

    CHECK(!async_responder);
    runloop.Next();
    async_responder.Respond();
    runloop.Run();
  }

  {
    using namespace std::chrono_literals;
    auto deadline = std::chrono::system_clock::now() + 1000h;
    server.Shutdown(deadline);
  }
  server_thread.join();
}

}  // namespace shk
