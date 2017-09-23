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
#include <rs/sum.h>
#include <rs/throw.h>
#include <rs-grpc/server.h>

#include "rsgrpctest.grpc.pb.h"
#include "test_util.h"

namespace shk {
namespace {

auto SumHandler(AnyPublisher<TestRequest> &&requests) {
  return Pipe(
      requests,
      Map([](TestRequest &&request) {
        return request.data();
      }),
      Sum(),
      Map(MakeTestResponse));
}

auto ImmediatelyFailingSumHandler(AnyPublisher<TestRequest> &&requests) {
  // Hack: unless requests is subscribed to, nothing happens. Would be nice to
  // fix this.
  requests.Subscribe(MakeSubscriber()).Request(ElementCount::Unbounded());

  return Throw(std::runtime_error("sum_fail"));
}

auto FailingSumHandler(AnyPublisher<TestRequest> &&requests) {
  return SumHandler(AnyPublisher<TestRequest>(Pipe(
      requests,
      Map([](TestRequest &&request) {
        if (request.data() == -1) {
          throw std::runtime_error("sum_fail");
        }
        return request;
      }))));
}

auto ClientStreamNoResponseHandler(AnyPublisher<TestRequest> &&requests) {
  // Hack: unless requests is subscribed to, nothing happens. Would be nice to
  // fix this.
  requests.Subscribe(MakeSubscriber()).Request(ElementCount::Unbounded());

  return Empty();
}

auto ClientStreamTwoResponsesHandler(AnyPublisher<TestRequest> &&requests) {
  // Hack: unless requests is subscribed to, nothing happens. Would be nice to
  // fix this. TODO(peck): Try to this unnecessary
  requests.Subscribe(MakeSubscriber()).Request(ElementCount::Unbounded());

  return Just(
      MakeTestResponse(1),
      MakeTestResponse(2));
}

auto ClientStreamEchoAllHandler(AnyPublisher<TestRequest> &&requests) {
  return Pipe(
      requests,
      Map([](TestRequest &&request) {
        return request.data();
      }),
      Map(MakeTestResponse));
}

}  // anonymous namespace

TEST_CASE("Client streaming RPC") {
  auto server_address = "unix:rs_grpc_test.socket";

  RsGrpcServer::Builder server_builder;
  server_builder.GrpcServerBuilder()
      .AddListeningPort(server_address, grpc::InsecureServerCredentials());

  std::atomic<int> hang_on_seen_elements(0);
  std::shared_ptr<AnySubscription> hung_subscription;

  server_builder.RegisterService<TestService::AsyncService>()
      .RegisterMethod(
          &TestService::AsyncService::RequestSum,
          &SumHandler)
      .RegisterMethod(
          &TestService::AsyncService::RequestImmediatelyFailingSum,
          &ImmediatelyFailingSumHandler)
      .RegisterMethod(
          &TestService::AsyncService::RequestFailingSum,
          &FailingSumHandler)
      .RegisterMethod(
          &TestService::AsyncService::RequestClientStreamNoResponse,
          &ClientStreamNoResponseHandler)
      .RegisterMethod(
          &TestService::AsyncService::RequestClientStreamTwoResponses,
          &ClientStreamTwoResponsesHandler)
      .RegisterMethod(
          &TestService::AsyncService::RequestClientStreamRequestZero,
          &RequestZeroHandler)
      .RegisterMethod(
          &TestService::AsyncService::RequestClientStreamHangOnZero,
          MakeHangOnZeroHandler(&hang_on_seen_elements, &hung_subscription))
      .RegisterMethod(
          &TestService::AsyncService::RequestClientStreamEchoAll,
          &ClientStreamEchoAllHandler);

  RsGrpcClient runloop;

  grpc::ResourceQuota quota;
  grpc::ChannelArguments channel_args;
  channel_args.SetResourceQuota(quota);

  auto channel = grpc::CreateCustomChannel(
      server_address,
      grpc::InsecureChannelCredentials(),
      channel_args);

  auto test_client = runloop.MakeClient(
      TestService::NewStub(channel));

  auto server = server_builder.BuildAndStart();
  std::thread server_thread([&] { server.Run(); });

  SECTION("no messages") {
    Run(&runloop, Pipe(
        test_client.Invoke(
            &TestService::Stub::AsyncSum,
            Empty()),
        Map([](TestResponse &&response) {
          CHECK(response.data() == 0);
          return "ignored";
        }),
        Count(),
        Map([](int count) {
          CHECK(count == 1);
          return "ignored";
        })));
  }

  SECTION("backpressure") {
    SECTION("call Invoke but don't request a value") {
      auto publisher = Pipe(
          test_client.Invoke(
              &TestService::Stub::AsyncSum, Empty()),
          Map([](TestResponse response) {
            CHECK(!"should not be invoked");
            return "ignored";
          }));
      std::shared_ptr<void> tag = RunExpectTimeout(&runloop, publisher);
    }

    SECTION("make call that never requests elements") {
      auto publisher = Pipe(
          test_client.Invoke(
              &TestService::Stub::AsyncClientStreamRequestZero,
              Just(MakeTestRequest(432))),
          Map([](TestResponse &&response) {
            CHECK(!"should not be invoked");
            return "ignored";
          }));
      auto error = RunExpectError(&runloop, publisher);
      CHECK(ExceptionMessage(error) == "Cancelled");
    }

    SECTION("make call that requests one element") {
      auto publisher = Pipe(
          test_client.Invoke(
              &TestService::Stub::AsyncClientStreamHangOnZero,
              Just(
                  MakeTestRequest(1),
                  MakeTestRequest(0),  // Hang on this one
                  MakeTestRequest(1))),
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
          test_client.Invoke(
              &TestService::Stub::AsyncClientStreamHangOnZero,
              Just(
                  MakeTestRequest(1),
                  MakeTestRequest(2),
                  MakeTestRequest(0),  // Hang on this one
                  MakeTestRequest(1))),
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
          test_client.Invoke(
              &TestService::Stub::AsyncClientStreamRequestZero,
              MakeInfiniteRequest()),
          Map([](TestResponse &&response) {
            CHECK(!"should not be invoked");
            return "ignored";
          }));
      std::shared_ptr<void> tag =
          RunExpectTimeout(&runloop, publisher, ElementCount::Unbounded());

      ShutdownAllowOutstandingCall(&server);
    }

    SECTION("violate backpressure in provided publisher") {
      auto publisher = Pipe(
          test_client.Invoke(
              &TestService::Stub::AsyncSum,
              MakePublisher([](auto &&subscriber) {
                // Emit element before it was asked for: streams should not do
                // this.
                subscriber.OnNext(MakeTestRequest(1));
                subscriber.OnNext(MakeTestRequest(2));
                return MakeSubscription();
              })));
      auto error = RunExpectError(&runloop, publisher);
      CHECK(ExceptionMessage(error) == "Backpressure violation");
    }
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
        auto call = test_client.Invoke(
            &TestService::Stub::AsyncClientStreamRequestZero,
            Empty());

        auto subscription = call.Subscribe(std::move(null_subscriber));
        subscription.Request(ElementCount::Unbounded());

        CHECK(runloop.Next());
        CHECK(runloop.Next());
        subscription.Cancel();
        CHECK(runloop.Next());

        ShutdownAllowOutstandingCall(&server);

        CHECK(cancelled == false);
      }

      SECTION("before Request") {
        auto call = test_client.Invoke(
            &TestService::Stub::AsyncSum,
            Never());

        auto subscription = call.Subscribe(std::move(null_subscriber));
        subscription.Cancel();
        subscription.Request(ElementCount::Unbounded());

        // There should be nothing on the runloop
        using namespace std::chrono_literals;
        auto deadline = std::chrono::system_clock::now() + 20ms;
        CHECK(runloop.Next(deadline) == grpc::CompletionQueue::TIMEOUT);

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

        auto call = test_client.Invoke(
            &TestService::Stub::AsyncClientStreamRequestZero,
            detect_cancel);

        auto subscription = call.Subscribe(std::move(null_subscriber));
        subscription.Request(ElementCount::Unbounded());
        subscription.Cancel();
        CHECK(subscription_cancelled);

        CHECK(cancelled == false);

        ShutdownAllowOutstandingCall(&server);

        runloop.Shutdown();
        runloop.Run();
      }
    }
  }

  SECTION("one message") {
    Run(&runloop, Pipe(
        test_client.Invoke(
            &TestService::Stub::AsyncSum,
            Just(MakeTestRequest(1337))),
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
    auto error = RunExpectError(&runloop, test_client
        .Invoke(
            &TestService::Stub::AsyncSum,
            Throw(std::runtime_error("test_error"))));
    CHECK(ExceptionMessage(error) == "test_error");
  }

  SECTION("stream failed after one message") {
    auto error = RunExpectError(&runloop, test_client
        .Invoke(
            &TestService::Stub::AsyncSum,
            Concat(
                Just(MakeTestRequest(0)),
                Throw(std::runtime_error("test_error")))));
    CHECK(ExceptionMessage(error) == "test_error");
  }

  SECTION("one message (echo all)") {
    // This test is there to try to trigger a reference cycle memory leak.
    Run(&runloop, Pipe(
        test_client.Invoke(
            &TestService::Stub::AsyncClientStreamEchoAll,
            Just(MakeTestRequest(13))),
        Map([](TestResponse &&response) {
          CHECK(response.data() == 13);
          return "ignored";
        }),
        Count(),
        Map([](int count) {
          CHECK(count == 1);
          return "ignored";
        })));
  }

  SECTION("two messages") {
    Run(&runloop, Pipe(
        test_client.Invoke(
            &TestService::Stub::AsyncSum,
            Just(MakeTestRequest(13), MakeTestRequest(7))),
        Map([](TestResponse &&response) {
          CHECK(response.data() == 20);
          return "ignored";
        }),
        Count(),
        Map([](int count) {
          CHECK(count == 1);
          return "ignored";
        })));
  }

  SECTION("no messages then fail") {
    auto error = RunExpectError(&runloop, Pipe(
        test_client.Invoke(
            &TestService::Stub::AsyncImmediatelyFailingSum,
            Empty()),
        Map([](TestResponse &&response) {
          CHECK(!"should not happen");
          return "unused";
        })));
    CHECK(ExceptionMessage(error) == "sum_fail");
  }

  SECTION("message then immediately fail") {
    auto error = RunExpectError(&runloop, Pipe(
        test_client.Invoke(
            &TestService::Stub::AsyncImmediatelyFailingSum,
            Just(MakeTestRequest(1337))),
        Map([](TestResponse &&response) {
          CHECK(!"should not happen");
          return "unused";
        })));
    CHECK(ExceptionMessage(error) == "sum_fail");
  }

  SECTION("fail on first message") {
    auto error = RunExpectError(&runloop, Pipe(
        test_client.Invoke(
            &TestService::Stub::AsyncFailingSum,
            Just(MakeTestRequest(-1))),
        Map([](TestResponse &&response) {
          CHECK(!"should not happen");
          return "unused";
        })));
    CHECK(ExceptionMessage(error) == "sum_fail");
  }

  SECTION("fail on second message") {
    auto error = RunExpectError(&runloop, Pipe(
        test_client.Invoke(
            &TestService::Stub::AsyncFailingSum,
            Just(MakeTestRequest(0), MakeTestRequest(-1))),
        Map([](TestResponse &&response) {
          CHECK(!"should not happen");
          return "unused";
        })));
    CHECK(ExceptionMessage(error) == "sum_fail");
  }

  SECTION("fail because of no response") {
    auto error = RunExpectError(&runloop, Pipe(
        test_client.Invoke(
            &TestService::Stub::AsyncClientStreamNoResponse,
            Just(MakeTestRequest(0))),
        Map([](TestResponse &&response) {
          CHECK(!"should not happen");
          return "unused";
        })));
    CHECK(ExceptionMessage(error) == "No response");
  }

  SECTION("fail because of two responses") {
    auto error = RunExpectError(&runloop, Pipe(
        test_client.Invoke(
            &TestService::Stub::AsyncClientStreamTwoResponses,
            Just(MakeTestRequest(0))),
        Map([](TestResponse &&response) {
          CHECK(!"should not happen");
          return "unused";
        })));
    CHECK(ExceptionMessage(error) == "Too many responses");
  }

  SECTION("two calls") {
    auto call_0 = Pipe(
        test_client.Invoke(
            &TestService::Stub::AsyncSum,
            Just(MakeTestRequest(13), MakeTestRequest(7))),
        Map([](TestResponse &&response) {
          CHECK(response.data() == 20);
          return "ignored";
        }),
        Count(),
        Map([](int count) {
          CHECK(count == 1);
          return "ignored";
        }));

    auto call_1 = Pipe(
        test_client.Invoke(
            &TestService::Stub::AsyncSum,
            Just(MakeTestRequest(10), MakeTestRequest(2))),
        Map([](TestResponse &&response) {
          CHECK(response.data() == 12);
          return "ignored";
        }),
        Count(),
        Map([](int count) {
          CHECK(count == 1);
          return "ignored";
        }));

    Run(&runloop, Merge<const char *>(call_0, call_1));
  }

  SECTION("same call twice") {
    auto call = Pipe(
        test_client.Invoke(
            &TestService::Stub::AsyncSum,
            Just(MakeTestRequest(13), MakeTestRequest(7))),
        Map([](TestResponse &&response) {
          CHECK(response.data() == 20);
          return "ignored";
        }),
        Count(),
        Map([](int count) {
          CHECK(count == 1);
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
