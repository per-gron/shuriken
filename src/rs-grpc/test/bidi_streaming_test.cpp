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

#include <grpc++/resource_quota.h>
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

class BidiStreamingTestServer : public BidiStreamingTest {
 public:
  BidiStreamingTestServer(
      std::atomic<int> *hang_on_seen_elements,
      std::shared_ptr<AnySubscription> *hung_subscription)
      : hang_on_seen_elements_(*hang_on_seen_elements),
        hung_subscription_(*hung_subscription) {}

  AnyPublisher</*stream*/ TestResponse> CumulativeSum(
      const CallContext &ctx, AnyPublisher<TestRequest> &&requests) override {
    return AnyPublisher<TestResponse>(Pipe(
      requests,
      Map([](TestRequest &&request) {
        return request.data();
      }),
      Scan(0, [](int x, int y) { return x + y; }),
      Map(MakeTestResponse)));
  }

  AnyPublisher</*stream*/ TestResponse> ImmediatelyFailingCumulativeSum(
      const CallContext &ctx, AnyPublisher<TestRequest> &&requests) override {
    // Hack: unless requests is subscribed to, nothing happens. Would be nice to
    // fix this.
    requests.Subscribe(MakeSubscriber()).Request(ElementCount::Unbounded());

    return AnyPublisher<TestResponse>(
        Throw(std::runtime_error("cumulative_sum_fail")));
  }

  AnyPublisher</*stream*/ TestResponse> FailingCumulativeSum(
      const CallContext &ctx, AnyPublisher<TestRequest> &&requests) override {
    return AnyPublisher<TestResponse>(
        CumulativeSum(ctx, AnyPublisher<TestRequest>(Pipe(
            requests,
            Map([](TestRequest &&request) {
              if (request.data() == -1) {
                throw std::runtime_error("cumulative_sum_fail");
              }
              return request;
            })))));
  }

  AnyPublisher</*stream*/ TestResponse> BidiStreamRequestZero(
      const CallContext &ctx, AnyPublisher<TestRequest> &&requests) override {
    return AnyPublisher<TestResponse>(
        RequestZeroHandler(ctx, std::move(requests)));
  }

  AnyPublisher</*stream*/ TestResponse> BidiStreamHangOnZero(
      const CallContext &ctx, AnyPublisher<TestRequest> &&requests) override {
    return AnyPublisher<TestResponse>(
        MakeHangOnZeroHandler(&hang_on_seen_elements_, &hung_subscription_)(
            ctx, std::move(requests)));
  }

  AnyPublisher</*stream*/ TestResponse> BidiStreamInfiniteResponse(
      const CallContext &ctx, AnyPublisher<TestRequest> &&requests) override {
    // Hack: unless requests is subscribed to, nothing happens. Would be nice to
    // fix this.
    requests.Subscribe(MakeSubscriber()).Request(ElementCount::Unbounded());

    return AnyPublisher<TestResponse>(MakeInfiniteResponse());
  }

  AnyPublisher</*stream*/ TestResponse> BidiStreamBackpressureViolation(
      const CallContext &ctx, AnyPublisher<TestRequest> &&requests) override {
    return AnyPublisher<TestResponse>(MakePublisher([](auto &&subscriber) {
      // Emit element before it was asked for: streams should not do
      // this.
      subscriber.OnNext(MakeTestResponse(1));
      subscriber.OnNext(MakeTestResponse(2));
      subscriber.OnNext(MakeTestResponse(3));
      return MakeSubscription();
    }));
  }

 private:
  std::atomic<int> &hang_on_seen_elements_;
  std::shared_ptr<AnySubscription> &hung_subscription_;
};

}  // anonymous namespace

TEST_CASE("Bidi streaming RPC") {
  InitTests();

  auto server_address = "unix:rs_grpc_test.socket";

  RsGrpcServer::Builder server_builder;
  server_builder.GrpcServerBuilder()
      .AddListeningPort(server_address, ::grpc::InsecureServerCredentials());

  std::atomic<int> hang_on_seen_elements(0);
  std::shared_ptr<AnySubscription> hung_subscription;

  server_builder
      .RegisterService(
          std::unique_ptr<BidiStreamingTestServer>(
              new BidiStreamingTestServer(
                  &hang_on_seen_elements,
                  &hung_subscription)))
      .RegisterMethod(
          &grpc::BidiStreamingTest::AsyncService::RequestCumulativeSum,
          &BidiStreamingTestServer::CumulativeSum)
      .RegisterMethod(
          &grpc::BidiStreamingTest::AsyncService::
              RequestImmediatelyFailingCumulativeSum,
          &BidiStreamingTestServer::ImmediatelyFailingCumulativeSum)
      .RegisterMethod(
          &grpc::BidiStreamingTest::AsyncService::RequestFailingCumulativeSum,
          &BidiStreamingTestServer::FailingCumulativeSum)
      .RegisterMethod(
          &grpc::BidiStreamingTest::AsyncService::RequestBidiStreamRequestZero,
          &BidiStreamingTestServer::BidiStreamRequestZero)
      .RegisterMethod(
          &grpc::BidiStreamingTest::AsyncService::RequestBidiStreamHangOnZero,
          &BidiStreamingTestServer::BidiStreamHangOnZero)
      .RegisterMethod(
          &grpc::BidiStreamingTest::AsyncService::
              RequestBidiStreamInfiniteResponse,
          &BidiStreamingTestServer::BidiStreamInfiniteResponse)
      .RegisterMethod(
          &grpc::BidiStreamingTest::AsyncService::
              RequestBidiStreamBackpressureViolation,
          &BidiStreamingTestServer::BidiStreamBackpressureViolation);

  RsGrpcClientRunloop runloop;
  CallContext ctx = runloop.CallContext();

  ::grpc::ResourceQuota quota;
  ::grpc::ChannelArguments channel_args;
  channel_args.SetResourceQuota(quota);

  auto channel = ::grpc::CreateCustomChannel(
      server_address,
      ::grpc::InsecureChannelCredentials(),
      channel_args);

  auto test_client = BidiStreamingTest::NewClient(channel);

  auto server = server_builder.BuildAndStart();
  std::thread server_thread([&] { server.Run(); });

  SECTION("no messages") {
    Run(&runloop, Pipe(
        test_client->CumulativeSum(ctx, AnyPublisher<TestRequest>(Empty())),
        Count(),
        Map([](int count) {
          CHECK(count == 0);
          return "ignored";
        })));
  }

  SECTION("cancellation") {
    bool cancelled = false;
    auto null_subscriber = MakeSubscriber(
        [](auto &&) {
          CHECK(!"OnNext should not be called");
        },
        [&cancelled](std::exception_ptr error) {
          CHECK(ExceptionMessage(error) == "Cancelled");
          cancelled = true;
        },
        []() {
          CHECK(!"OnComplete should not be called");
        });

    SECTION("from client side") {
      SECTION("after Request") {
        auto call = test_client->BidiStreamRequestZero(
            ctx, AnyPublisher<TestRequest>(Empty()));

        auto subscription = call.Subscribe(std::move(null_subscriber));
        subscription.Request(ElementCount::Unbounded());

        CHECK(runloop.Next());
        CHECK(runloop.Next());
        subscription.Cancel();
        CHECK(runloop.Next());

        ShutdownAllowOutstandingCall(&server);

        CHECK(cancelled == false);

        runloop.Shutdown();
        runloop.Run();
      }

      SECTION("before Request") {
        auto call = test_client->CumulativeSum(
            ctx,
            AnyPublisher<TestRequest>(Never()));

        auto subscription = call.Subscribe(std::move(null_subscriber));
        subscription.Cancel();
        subscription.Request(ElementCount::Unbounded());

        // There should be nothing on the runloop
        using namespace std::chrono_literals;
        auto deadline = std::chrono::system_clock::now() + 20ms;
        CHECK(runloop.Next(deadline) == ::grpc::CompletionQueue::TIMEOUT);

        CHECK(cancelled == false);
      }

      SECTION("cancel input stream") {
        bool subscription_cancelled = false;

        auto detect_cancel = MakePublisher(
            [&subscription_cancelled](auto &&subscriber) {
              return MakeSubscription(
                  [](ElementCount) {},
                  [&subscription_cancelled] {
                    subscription_cancelled = true;
                  });
            });

        auto call = test_client->BidiStreamRequestZero(
            ctx,
            AnyPublisher<TestRequest>(detect_cancel));

        auto subscription = call.Subscribe(std::move(null_subscriber));
        subscription.Request(ElementCount::Unbounded());
        subscription.Cancel();
        CHECK(subscription_cancelled);

        ShutdownAllowOutstandingCall(&server);

        CHECK(cancelled == false);

        runloop.Shutdown();
        runloop.Run();
      }
    }
  }

  SECTION("backpressure") {
    int latest_seen_response = 0;
    auto publisher = Pipe(
        test_client->CumulativeSum(
            ctx,
            AnyPublisher<TestRequest>(Repeat(MakeTestRequest(1), 10))),
        Map([&latest_seen_response](TestResponse &&response) {
          CHECK(++latest_seen_response == response.data());
          return "ignored";
        }));

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

    SECTION("make call that never requests elements") {
      auto publisher = Pipe(
          test_client->BidiStreamRequestZero(
              ctx,
              AnyPublisher<TestRequest>(Just(MakeTestRequest(432)))),
          Map([](TestResponse &&response) {
            CHECK(!"should not be invoked");
            return "ignored";
          }));
      auto error = RunExpectError(&runloop, publisher);
      CHECK(ExceptionMessage(error) == "Cancelled");
    }

    SECTION("make call that requests one element") {
      auto publisher = Pipe(
          test_client->BidiStreamHangOnZero(
              ctx,
              AnyPublisher<TestRequest>(Just(
                  MakeTestRequest(1),
                  MakeTestRequest(0),  // Hang on this one
                  MakeTestRequest(1)))),
          Map([](TestResponse &&response) {
            CHECK(!"should not be invoked");
            return "ignored";
          }));
      std::shared_ptr<void> tag =
          RunExpectTimeout(&runloop, publisher, ElementCount::Unbounded());

      CHECK(hang_on_seen_elements == 2);

      CHECK(hung_subscription);
      hung_subscription.reset();
    }

    SECTION("make call that requests two elements") {
      auto publisher = Pipe(
          test_client->BidiStreamHangOnZero(
              ctx,
              AnyPublisher<TestRequest>(Just(
                  MakeTestRequest(1),
                  MakeTestRequest(2),
                  MakeTestRequest(0),  // Hang on this one
                  MakeTestRequest(1)))),
          Map([](TestResponse &&response) {
            CHECK(!"should not be invoked");
            return "ignored";
          }));
      std::shared_ptr<void> tag =
          RunExpectTimeout(&runloop, publisher, ElementCount::Unbounded());

      CHECK(hang_on_seen_elements == 3);

      CHECK(hung_subscription);
      hung_subscription.reset();
    }

    SECTION("make call with unlimited stream") {
      // This test is supposed to push messages to the server until the buffers
      // get full. The default buffer size in gRPC is so big that the test takes
      // a lot of time to complete. This reduces the buffer size so that this
      // test completes reasonably quickly.
      quota.Resize(4096);

      // If client-side rs-grpc violates backpressure requirements by requesting
      // an unbounded number of elements from this infinite stream (which the
      // server does not do), then this will smash the stack or run out of
      // memory.
      auto publisher = Pipe(
          test_client->BidiStreamRequestZero(
              ctx,
              AnyPublisher<TestRequest>(MakeInfiniteRequest())),
          Map([](TestResponse &&response) {
            CHECK(!"should not be invoked");
            return "ignored";
          }));
      std::shared_ptr<void> tag =
          RunExpectTimeout(&runloop, publisher, ElementCount::Unbounded());

      ShutdownAllowOutstandingCall(&server);
    }

    SECTION("Request one element from infinite response stream") {
      auto request = test_client->BidiStreamInfiniteResponse(
          ctx,
          AnyPublisher<TestRequest>(Empty()));

      auto subscription = request.Subscribe(MakeSubscriber());
      subscription.Request(ElementCount(1));

      CHECK(runloop.Next());
      CHECK(runloop.Next());
      CHECK(runloop.Next());

      ShutdownAllowOutstandingCall(&server);

      runloop.Shutdown();
      runloop.Run();
    }

    SECTION("violate backpressure in provided publisher (client side)") {
      auto publisher = Pipe(
          test_client->CumulativeSum(
              ctx,
              AnyPublisher<TestRequest>(MakePublisher([](auto &&subscriber) {
                // Emit element before it was asked for: streams should not do
                // this.
                subscriber.OnNext(MakeTestRequest(1));
                subscriber.OnNext(MakeTestRequest(2));
                return MakeSubscription();
              }))));
      auto error = RunExpectError(&runloop, publisher);
      CHECK(ExceptionMessage(error) == "Backpressure violation");
    }

    SECTION("violate backpressure in provided publisher (server side)") {
      auto publisher = Pipe(
          test_client->BidiStreamBackpressureViolation(
              ctx,
              AnyPublisher<TestRequest>(Empty())));
      auto error = RunExpectError(&runloop, publisher);
      CHECK(ExceptionMessage(error) == "Backpressure violation");
    }
  }

  SECTION("one message") {
    Run(&runloop, Pipe(
        test_client->CumulativeSum(
            ctx,
            AnyPublisher<TestRequest>(Just(MakeTestRequest(1337)))),
        Map([](TestResponse &&response) {
          CHECK(response.data() == 1337);
          return "ignored";
        }),
        Count(),
        Map([](int count) {
          CHECK(count == 1);
          return "ignored";
        })));
  }

  SECTION("immediately failed stream") {
    auto error = RunExpectError(&runloop, 
        test_client->CumulativeSum(
            ctx,
            AnyPublisher<TestRequest>(
                Throw(std::runtime_error("test_error")))));
    CHECK(ExceptionMessage(error) == "test_error");
  }

  SECTION("stream failed after one message") {
    auto error = RunExpectError(&runloop, test_client
        ->CumulativeSum(
            ctx,
            AnyPublisher<TestRequest>(Concat(
                Just(MakeTestRequest(0)),
                Throw(std::runtime_error("test_error"))))));
    CHECK(ExceptionMessage(error) == "test_error");
  }

  SECTION("two message") {
    Run(&runloop, Pipe(
        test_client->CumulativeSum(
            ctx,
            AnyPublisher<TestRequest>(
                Just(MakeTestRequest(10), MakeTestRequest(20)))),
        Map([](TestResponse &&response) {
          return response.data();
        }),
        Sum(),
        Map([](int sum) {
          CHECK(sum == 40); // (10) + (10 + 20)
          return "ignored";
        })));
  }

  SECTION("no messages then fail") {
    auto error = RunExpectError(&runloop, Pipe(
        test_client->ImmediatelyFailingCumulativeSum(
            ctx,
            AnyPublisher<TestRequest>(Empty())),
        Map([](TestResponse &&response) {
          CHECK(!"should not happen");
          return "unused";
        })));
    CHECK(ExceptionMessage(error) == "cumulative_sum_fail");
  }

  SECTION("message then immediately fail") {
    auto error = RunExpectError(&runloop, Pipe(
        test_client->ImmediatelyFailingCumulativeSum(
            ctx,
            AnyPublisher<TestRequest>(Just(MakeTestRequest(1337)))),
        Map([](TestResponse &&response) {
          CHECK(!"should not happen");
          return "unused";
        })));
    CHECK(ExceptionMessage(error) == "cumulative_sum_fail");
  }

  SECTION("fail on first message") {
    auto error = RunExpectError(&runloop, Pipe(
        test_client->FailingCumulativeSum(
            ctx,
            AnyPublisher<TestRequest>(Just(MakeTestRequest(-1)))),
        Map([](TestResponse &&response) {
          CHECK(!"should not happen");
          return "unused";
        })));
    CHECK(ExceptionMessage(error) == "cumulative_sum_fail");
  }

  SECTION("fail on second message") {
    int count = 0;
    auto error = RunExpectError(&runloop, Pipe(
        test_client->FailingCumulativeSum(
            ctx,
            AnyPublisher<TestRequest>(
                Just(MakeTestRequest(321), MakeTestRequest(-1)))),
        Map([&count](TestResponse &&response) {
          CHECK(response.data() == 321);
          count++;
          return "unused";
        })));
    CHECK(ExceptionMessage(error) == "cumulative_sum_fail");
    CHECK(count == 1);
  }

  SECTION("two calls") {
    auto call_0 = Pipe(
        test_client->CumulativeSum(
            ctx,
            AnyPublisher<TestRequest>(
                Just(MakeTestRequest(10), MakeTestRequest(20)))),
        Map([](TestResponse &&response) {
          return response.data();
        }),
        Sum(),
        Map([](int sum) {
          CHECK(sum == 40); // (10) + (10 + 20)
          return "ignored";
        }));

    auto call_1 = Pipe(
        test_client->CumulativeSum(
            ctx,
            AnyPublisher<TestRequest>(
                Just(MakeTestRequest(1), MakeTestRequest(2)))),
        Map([](TestResponse &&response) {
          return response.data();
        }),
        Sum(),
        Map([](int sum) {
          CHECK(sum == 4); // (1) + (1 + 2)
          return "ignored";
        }));

    Run(&runloop, Merge<const char *>(call_0, call_1));
  }

  SECTION("same call twice") {
    auto call = Pipe(
        test_client->CumulativeSum(
            ctx,
            AnyPublisher<TestRequest>(
                Just(MakeTestRequest(10), MakeTestRequest(20)))),
        Map([](TestResponse &&response) {
          return response.data();
        }),
        Sum(),
        Map([](int sum) {
          CHECK(sum == 40); // (10) + (10 + 20)
          return "ignored";
        }));

    Run(&runloop, Merge<const char *>(call, call));
  }

  {
    using namespace std::chrono_literals;
    auto deadline = std::chrono::system_clock::now() + 1000h;
    server.Shutdown(deadline);
  }
  server_thread.join();
}

}  // namespace shk
