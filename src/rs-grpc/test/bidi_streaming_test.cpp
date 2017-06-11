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

auto CumulativeSumHandler(Publisher<Flatbuffer<TestRequest>> requests) {
  return Pipe(
    requests,
    Map([](Flatbuffer<TestRequest> request) {
      return request->data();
    }),
    Scan(0, [](int x, int y) { return x + y; }),
    Map(MakeTestResponse));
}

auto ImmediatelyFailingCumulativeSumHandler(
    Publisher<Flatbuffer<TestRequest>> requests) {
  // Hack: unless requests is subscribed to, nothing happens. Would be nice to
  // fix this.
  requests.Subscribe(MakeSubscriber()).Request(ElementCount::Unbounded());

  return Throw(std::runtime_error("cumulative_sum_fail"));
}

auto FailingCumulativeSumHandler(
    Publisher<Flatbuffer<TestRequest>> requests) {
  return CumulativeSumHandler(Publisher<Flatbuffer<TestRequest>>(Pipe(
      requests,
      Map([](Flatbuffer<TestRequest> request) {
        if (request->data() == -1) {
          throw std::runtime_error("cumulative_sum_fail");
        }
        return request;
      }))));
}

}  // anonymous namespace

TEST_CASE("Bidi streaming RPC") {
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
          &TestService::AsyncService::RequestCumulativeSum,
          &CumulativeSumHandler)
      .RegisterMethod(
          &TestService::AsyncService::RequestImmediatelyFailingCumulativeSum,
          &ImmediatelyFailingCumulativeSumHandler)
      .RegisterMethod(
          &TestService::AsyncService::RequestFailingCumulativeSum,
          &FailingCumulativeSumHandler)
      .RegisterMethod(
          &TestService::AsyncService::RequestBidiStreamRequestZero,
          &RequestZeroHandler)
      .RegisterMethod(
          &TestService::AsyncService::RequestBidiStreamHangOnZero,
          MakeHangOnZeroHandler(&hang_on_seen_elements));

  RsGrpcClient runloop;

  auto channel = grpc::CreateChannel(
      server_address, grpc::InsecureChannelCredentials());

  auto test_client = runloop.MakeClient(
      TestService::NewStub(channel));

  auto server = server_builder.BuildAndStart();
  std::thread server_thread([&] { server.Run(); });

  SECTION("server streaming") {
    // TODO(peck): Remove this test, it's duplicated
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

  SECTION("no messages") {
    Run(&runloop, Pipe(
        test_client.Invoke(
            &TestService::Stub::AsyncCumulativeSum,
            Empty()),
        Count(),
        Map([](int count) {
          CHECK(count == 0);
          return "ignored";
        })));
  }

  SECTION("cancellation") {
    SECTION("from client side") {
      SECTION("after Request") {
        auto call = test_client.Invoke(
            &TestService::Stub::AsyncBidiStreamRequestZero,
            Empty());

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

        CHECK(runloop.Next());
        CHECK(runloop.Next());
        subscription.Cancel();
        CHECK(runloop.Next());

        ShutdownAllowOutstandingCall(&server);
      }

      SECTION("before Request") {
        auto call = test_client.Invoke(
            &TestService::Stub::AsyncCumulativeSum,
            Never());

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
            &TestService::Stub::AsyncCumulativeSum,
            Repeat(MakeTestRequest(1), 10)),
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

      ShutdownAllowOutstandingCall(&server);
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

    SECTION("make call that never requests elements") {
      auto publisher = Pipe(
          test_client.Invoke(
              &TestService::Stub::AsyncBidiStreamRequestZero,
              Just(MakeTestRequest(432))),
          Map([](Flatbuffer<TestResponse> response) {
            CHECK(!"should not be invoked");
            return "ignored";
          }));
      RunExpectTimeout(&runloop, publisher, ElementCount::Unbounded());

      ShutdownAllowOutstandingCall(&server);
    }

    SECTION("make call that requests one element") {
      auto publisher = Pipe(
          test_client.Invoke(
              &TestService::Stub::AsyncBidiStreamHangOnZero,
              Just(
                  MakeTestRequest(1),
                  MakeTestRequest(0),  // Hang on this one
                  MakeTestRequest(1))),
          Map([](Flatbuffer<TestResponse> response) {
            CHECK(!"should not be invoked");
            return "ignored";
          }));
      RunExpectTimeout(&runloop, publisher, ElementCount::Unbounded());

      CHECK(hang_on_seen_elements == 2);

      ShutdownAllowOutstandingCall(&server);
    }

    SECTION("make call that requests two elements") {
      auto publisher = Pipe(
          test_client.Invoke(
              &TestService::Stub::AsyncBidiStreamHangOnZero,
              Just(
                  MakeTestRequest(1),
                  MakeTestRequest(2),
                  MakeTestRequest(0),  // Hang on this one
                  MakeTestRequest(1))),
          Map([](Flatbuffer<TestResponse> response) {
            CHECK(!"should not be invoked");
            return "ignored";
          }));
      RunExpectTimeout(&runloop, publisher, ElementCount::Unbounded());

      CHECK(hang_on_seen_elements == 3);

      ShutdownAllowOutstandingCall(&server);
    }
  }

  SECTION("one message") {
    Run(&runloop, Pipe(
        test_client.Invoke(
            &TestService::Stub::AsyncCumulativeSum,
            Just(MakeTestRequest(1337))),
        Map([](Flatbuffer<TestResponse> response) {
          CHECK(response->data() == 1337);
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
        test_client.Invoke(
            &TestService::Stub::AsyncCumulativeSum,
            Throw(std::runtime_error("test_error"))));
    CHECK(ExceptionMessage(error) == "test_error");
  }

  SECTION("stream failed after one message") {
    auto error = RunExpectError(&runloop, test_client
        .Invoke(
            &TestService::Stub::AsyncCumulativeSum,
            Concat(
                Just(MakeTestRequest(0)),
                Throw(std::runtime_error("test_error")))));
    CHECK(ExceptionMessage(error) == "test_error");
  }

  SECTION("two message") {
    Run(&runloop, Pipe(
        test_client.Invoke(
            &TestService::Stub::AsyncCumulativeSum,
            Just(MakeTestRequest(10), MakeTestRequest(20))),
        Map([](Flatbuffer<TestResponse> response) {
          return response->data();
        }),
        Sum(),
        Map([](int sum) {
          CHECK(sum == 40); // (10) + (10 + 20)
          return "ignored";
        })));
  }

  SECTION("no messages then fail") {
    auto error = RunExpectError(&runloop, Pipe(
        test_client.Invoke(
            &TestService::Stub::AsyncImmediatelyFailingCumulativeSum,
            Empty()),
        Map([](Flatbuffer<TestResponse> response) {
          CHECK(!"should not happen");
          return "unused";
        })));
    CHECK(ExceptionMessage(error) == "cumulative_sum_fail");
  }

  SECTION("message then immediately fail") {
    auto error = RunExpectError(&runloop, Pipe(
        test_client.Invoke(
            &TestService::Stub::AsyncImmediatelyFailingCumulativeSum,
            Just(MakeTestRequest(1337))),
        Map([](Flatbuffer<TestResponse> response) {
          CHECK(!"should not happen");
          return "unused";
        })));
    CHECK(ExceptionMessage(error) == "cumulative_sum_fail");
  }

  SECTION("fail on first message") {
    auto error = RunExpectError(&runloop, Pipe(
        test_client.Invoke(
            &TestService::Stub::AsyncFailingCumulativeSum,
            Just(MakeTestRequest(-1))),
        Map([](Flatbuffer<TestResponse> response) {
          CHECK(!"should not happen");
          return "unused";
        })));
    CHECK(ExceptionMessage(error) == "cumulative_sum_fail");
  }

  SECTION("fail on second message") {
    int count = 0;
    auto error = RunExpectError(&runloop, Pipe(
        test_client.Invoke(
            &TestService::Stub::AsyncFailingCumulativeSum,
            Just(MakeTestRequest(321), MakeTestRequest(-1))),
        Map([&count](Flatbuffer<TestResponse> response) {
          CHECK(response->data() == 321);
          count++;
          return "unused";
        })));
    CHECK(ExceptionMessage(error) == "cumulative_sum_fail");
    CHECK(count == 1);
  }

  SECTION("two calls") {
    auto call_0 = Pipe(
        test_client.Invoke(
            &TestService::Stub::AsyncCumulativeSum,
            Just(MakeTestRequest(10), MakeTestRequest(20))),
        Map([](Flatbuffer<TestResponse> response) {
          return response->data();
        }),
        Sum(),
        Map([](int sum) {
          CHECK(sum == 40); // (10) + (10 + 20)
          return "ignored";
        }));

    auto call_1 = Pipe(
        test_client.Invoke(
            &TestService::Stub::AsyncCumulativeSum,
            Just(MakeTestRequest(1), MakeTestRequest(2))),
        Map([](Flatbuffer<TestResponse> response) {
          return response->data();
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
        test_client.Invoke(
            &TestService::Stub::AsyncCumulativeSum,
            Just(MakeTestRequest(10), MakeTestRequest(20))),
        Map([](Flatbuffer<TestResponse> response) {
          return response->data();
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
