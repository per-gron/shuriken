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
#include <rs-grpc/rs_grpc.h>

#include "rsgrpctest.grpc.fb.h"

using namespace RsGrpcTest;

namespace shk {
namespace {

template <typename T>
using Flatbuffer = flatbuffers::grpc::Message<T>;

Flatbuffer<TestRequest> MakeTestRequest(int data) {
  flatbuffers::grpc::MessageBuilder fbb;
  auto test_request = CreateTestRequest(fbb, data);
  fbb.Finish(test_request);
  return fbb.GetMessage<TestRequest>();
}

Flatbuffer<TestResponse> MakeTestResponse(int data) {
  flatbuffers::grpc::MessageBuilder fbb;
  auto test_response = CreateTestResponse(fbb, data);
  fbb.Finish(test_response);
  return fbb.GetMessage<TestResponse>();
}

auto DoubleHandler(Flatbuffer<TestRequest> request) {
  return Just(MakeTestResponse(request->data() * 2));
}

auto UnaryFailHandler(Flatbuffer<TestRequest> request) {
  return Throw(std::runtime_error("unary_fail"));
}

auto UnaryNoResponseHandler(Flatbuffer<TestRequest> request) {
  return Empty();
}

auto UnaryTwoResponsesHandler(Flatbuffer<TestRequest> request) {
  return Just(
      MakeTestResponse(1),
      MakeTestResponse(2));
}

auto UnaryHangHandler(Flatbuffer<TestRequest> request) {
  return Never();
}

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

auto SumHandler(Publisher<Flatbuffer<TestRequest>> requests) {
  return Pipe(
      requests,
      Map([](Flatbuffer<TestRequest> request) {
        return request->data();
      }),
      Sum(),
      Map(MakeTestResponse));
}

auto ImmediatelyFailingSumHandler(Publisher<Flatbuffer<TestRequest>> requests) {
  // Hack: unless requests is subscribed to, nothing happens. Would be nice to
  // fix this.
  requests.Subscribe(MakeSubscriber()).Request(ElementCount::Unbounded());

  return Throw(std::runtime_error("sum_fail"));
}

auto FailingSumHandler(Publisher<Flatbuffer<TestRequest>> requests) {
  return SumHandler(Publisher<Flatbuffer<TestRequest>>(Pipe(
      requests,
      Map([](Flatbuffer<TestRequest> request) {
        if (request->data() == -1) {
          throw std::runtime_error("sum_fail");
        }
        return request;
      }))));
}

auto ClientStreamNoResponseHandler(
    Publisher<Flatbuffer<TestRequest>> requests) {
  // Hack: unless requests is subscribed to, nothing happens. Would be nice to
  // fix this.
  requests.Subscribe(MakeSubscriber()).Request(ElementCount::Unbounded());

  return Empty();
}

auto ClientStreamTwoResponsesHandler(
    Publisher<Flatbuffer<TestRequest>> requests) {
  // Hack: unless requests is subscribed to, nothing happens. Would be nice to
  // fix this.
  requests.Subscribe(MakeSubscriber()).Request(ElementCount::Unbounded());

  return Just(
      MakeTestResponse(1),
      MakeTestResponse(2));
}

auto RequestZeroHandler(
    Publisher<Flatbuffer<TestRequest>> requests) {
  // The point of this test endpoint is to request some inputs, and verify that
  // it doesn't get more than that pushed to it. This endpoint never responds
  // so tests have to suceed by timing out.

  Subscription subscription = Subscription(requests.Subscribe(MakeSubscriber(
      [](auto &&) {
        CHECK(!"no elements should be published");
      },
      [](std::exception_ptr error) {
        CHECK(!"request should not fail");
      },
      []() {
        CHECK(!"request should not complete");
      })));
  subscription.Request(ElementCount(0));

  return Never();
}

auto MakeHangOnZeroHandler(
    std::atomic<int> *hang_on_seen_elements) {
  return [hang_on_seen_elements](Publisher<Flatbuffer<TestRequest>> requests) {
    // The point of this test endpoint is to request some inputs, and verify that
    // it doesn't get more than that pushed to it. This endpoint never responds
    // so tests have to suceed by timing out.

    bool seen_zero = false;
    std::shared_ptr<Subscription> sub =
        std::make_shared<Subscription>(MakeSubscription());
    *sub = Subscription(requests.Subscribe(MakeSubscriber(
        [&seen_zero, sub, hang_on_seen_elements](
            Flatbuffer<TestRequest> &&request) {
          (*hang_on_seen_elements)++;
          CHECK(!seen_zero);
          if (request->data() == 0) {
            seen_zero = true;
          } else {
            sub->Request(ElementCount(1));
          }
        },
        [](std::exception_ptr error) {
          CHECK(!"requests should not fail");
        },
        []() {
          CHECK(!"requests should not complete");
        })));
    sub->Request(ElementCount(1));

    return Never();
  };
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

TEST_CASE("RsGrpc") {
  // TODO(peck): Add support for cancellation (cancel is called unsubscribe)
  // TODO(peck): Add support for timeouts
  // TODO(peck): Add support for backpressure (streaming output requires only
  //     one outstanding request at a time. Not possible atm.)
  // TODO(peck): Test
  //  * finishing bidi and unidirectional streams in different orders
  //  * go through the code and look for stuff
  //  * what happens if writesdone is not called? Does the server stall then?

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
          &UnaryHangHandler)
      .RegisterMethod(
          &TestService::AsyncService::RequestRepeat,
          &RepeatHandler)
      .RegisterMethod(
          &TestService::AsyncService::RequestRepeatThenFail,
          &RepeatThenFailHandler)
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
          MakeHangOnZeroHandler(&hang_on_seen_elements))
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

  const auto run = [&](
      const auto &publisher,
      std::function<void (Subscription &)> subscribe = nullptr) {
    auto subscription = Subscription(publisher
        .Subscribe(MakeSubscriber(
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
            })));
    if (subscribe) {
      subscribe(subscription);
    } else {
      subscription.Request(ElementCount::Unbounded());
    }

    runloop.Run();
  };

  const auto run_expect_error = [&](
      const auto &publisher,
      std::function<void (Subscription &)> subscribe = nullptr) {
    std::exception_ptr captured_error;
    auto subscription = Subscription(publisher
        .Subscribe(MakeSubscriber(
            [](auto &&) {
              // Ignore OnNext
            },
            [&runloop, &captured_error](std::exception_ptr error) {
              runloop.Shutdown();
              captured_error = error;
            },
            [&runloop]() {
              CHECK(!"request should fail");
              runloop.Shutdown();
            })));
    if (subscribe) {
      subscribe(subscription);
    } else {
      subscription.Request(ElementCount::Unbounded());
    }

    runloop.Run();

    REQUIRE(captured_error);
    return captured_error;
  };

  const auto run_expect_timeout = [&](
      const auto &publisher,
      ElementCount count = ElementCount(0)) {
    auto subscription = Subscription(publisher
        .Subscribe(MakeSubscriber(
            [](auto &&) {
              // Ignore OnNext
            },
            [](std::exception_ptr error) {
              CHECK(!"request should not fail");
            },
            []() {
              CHECK(!"request should not finish");
            })));

    subscription.Request(count);
    for (;;) {
      using namespace std::chrono_literals;
      auto deadline = std::chrono::system_clock::now() + 50ms;
      if (runloop.Next(deadline) == grpc::CompletionQueue::TIMEOUT) {
        break;
      }
    }
  };

  // TODO(peck): Test what happens when calling unimplemented endpoint. I think
  // right now it just waits forever, which is not nice at all.

  SECTION("unary RPC") {
    SECTION("direct") {
      run(Pipe(
          test_client.Invoke(
              &TestService::Stub::AsyncDouble, MakeTestRequest(123)),
          Map([](Flatbuffer<TestResponse> response) {
            CHECK(response->data() == 123 * 2);
            return "ignored";
          })));
    }

    SECTION("backpressure") {
      SECTION("call Invoke but don't request a value") {
        auto publisher = Pipe(
            test_client.Invoke(
                &TestService::Stub::AsyncDouble, MakeTestRequest(123)),
            Map([](Flatbuffer<TestResponse> response) {
              CHECK(!"should not be invoked");
              return "ignored";
            }));
        run_expect_timeout(publisher);
      }
    }

    SECTION("Request twice") {
      auto request = Pipe(
          test_client.Invoke(
              &TestService::Stub::AsyncDouble, MakeTestRequest(123)),
          Map([](Flatbuffer<TestResponse> response) {
            CHECK(response->data() == 123 * 2);
            return "ignored";
          }));
      run(request, [](Subscription &sub) {
        sub.Request(ElementCount(1));
        sub.Request(ElementCount(1));
      });
    }

    SECTION("failed RPC") {
      auto error = run_expect_error(Pipe(
          test_client
              .Invoke(&TestService::Stub::AsyncUnaryFail, MakeTestRequest(0)),
          Map([](Flatbuffer<TestResponse> response) {
            CHECK(!"should not happen");
            return "unused";
          })));
      CHECK(ExceptionMessage(error) == "unary_fail");
    }

    SECTION("failed RPC because of no response") {
      auto error = run_expect_error(Pipe(
          test_client.Invoke(
              &TestService::Stub::AsyncUnaryNoResponse, MakeTestRequest(0)),
          Map([](Flatbuffer<TestResponse> response) {
            CHECK(!"should not happen");
            return "unused";
          })));
      CHECK(ExceptionMessage(error) == "No response");
    }

    SECTION("failed RPC because of two responses") {
      auto error = run_expect_error(Pipe(
          test_client.Invoke(
              &TestService::Stub::AsyncUnaryTwoResponses,
              MakeTestRequest(0)),
          Map([](Flatbuffer<TestResponse> response) {
            CHECK(!"should not happen");
            return "unused";
          })));
      CHECK(ExceptionMessage(error) == "Too many responses");
    }

    SECTION("RPC that never completes") {
      auto call = test_client.Invoke(
          &TestService::Stub::AsyncUnaryHang,
          MakeTestRequest(0));

      auto subscription = call
          .Subscribe(MakeSubscriber(
              [](auto &&) {
                CHECK(!"OnNext should not be called");
              },
              [](std::exception_ptr error) {
                CHECK(!"OnError should not be called");
              },
              []() {
                CHECK(!"OnComplete should not be called");
              }));
      subscription.Request(ElementCount::Unbounded());

      using namespace std::chrono_literals;
      auto deadline = std::chrono::system_clock::now() + 50ms;
      runloop.Next(deadline);
      runloop.Shutdown();
    }

    SECTION("delayed") {
      // This test can break if invoke doesn't take ownership of the request for
      // example.
      auto call = Pipe(
          test_client.Invoke(
              &TestService::Stub::AsyncDouble, MakeTestRequest(123)),
          Map([](Flatbuffer<TestResponse> response) {
            CHECK(response->data() == 123 * 2);
            return "ignored";
          }));
      run(call);
    }

    SECTION("two calls") {
      auto call_a = test_client.Invoke(
          &TestService::Stub::AsyncDouble, MakeTestRequest(123));
      auto call_b = test_client.Invoke(
          &TestService::Stub::AsyncDouble, MakeTestRequest(321));
      run(Pipe(
          Zip<std::tuple<Flatbuffer<TestResponse>, Flatbuffer<TestResponse>>>(
              call_a, call_b),
          Map(Splat([](
              Flatbuffer<TestResponse> a,
              Flatbuffer<TestResponse> b) {
            CHECK(a->data() == 123 * 2);
            CHECK(b->data() == 321 * 2);
            return "ignored";
          }))));
    }

    SECTION("same call twice") {
      auto call = test_client.Invoke(
          &TestService::Stub::AsyncDouble, MakeTestRequest(123));
      run(Pipe(
          Zip<std::tuple<Flatbuffer<TestResponse>, Flatbuffer<TestResponse>>>(
              call, call),
          Map(Splat([](
              Flatbuffer<TestResponse> a,
              Flatbuffer<TestResponse> b) {
            CHECK(a->data() == 123 * 2);
            CHECK(b->data() == 123 * 2);
            return "ignored";
          }))));
    }
  }

  SECTION("server streaming") {
    SECTION("no responses") {
      run(Pipe(
          test_client.Invoke(
              &TestService::Stub::AsyncRepeat, MakeTestRequest(0)),
          Map([](Flatbuffer<TestResponse> &&response) {
            // Should never be called; this should be a stream that ends
            // without any values
            CHECK(false);
            return "ignored";
          })));
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
          run_expect_timeout(publisher, ElementCount(i));
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
    }

    SECTION("one response") {
      run(Pipe(
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

      run(Merge<const char *>(check_count, check_sum));
    }

    SECTION("no responses then fail") {
      auto error = run_expect_error(Pipe(
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
      auto error = run_expect_error(Pipe(
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
      auto error = run_expect_error(Pipe(
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

      run(Merge<const char *>(responses_1, responses_2));
    }
  }

  SECTION("client streaming") {
    SECTION("no messages") {
      run(Pipe(
          test_client.Invoke(
              &TestService::Stub::AsyncSum,
              Empty()),
          Map([](Flatbuffer<TestResponse> &&response) {
            CHECK(response->data() == 0);
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
            Map([](Flatbuffer<TestResponse> response) {
              CHECK(!"should not be invoked");
              return "ignored";
            }));
        run_expect_timeout(publisher);
      }

      SECTION("make call that never requests elements") {
        auto publisher = Pipe(
            test_client.Invoke(
                &TestService::Stub::AsyncClientStreamRequestZero,
                Just(MakeTestRequest(432))),
            Map([](Flatbuffer<TestResponse> response) {
              CHECK(!"should not be invoked");
              return "ignored";
            }));
        run_expect_timeout(publisher, ElementCount::Unbounded());
      }

      SECTION("make call that requests one element") {
        auto publisher = Pipe(
            test_client.Invoke(
                &TestService::Stub::AsyncClientStreamHangOnZero,
                Just(
                    MakeTestRequest(1),
                    MakeTestRequest(0),  // Hang on this one
                    MakeTestRequest(1))),
            Map([](Flatbuffer<TestResponse> response) {
              CHECK(!"should not be invoked");
              return "ignored";
            }));
        run_expect_timeout(publisher, ElementCount::Unbounded());

        CHECK(hang_on_seen_elements == 2);
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
            Map([](Flatbuffer<TestResponse> response) {
              CHECK(!"should not be invoked");
              return "ignored";
            }));
        run_expect_timeout(publisher, ElementCount::Unbounded());

        CHECK(hang_on_seen_elements == 3);
      }
    }

    SECTION("one message") {
      run(Pipe(
          test_client.Invoke(
              &TestService::Stub::AsyncSum,
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
      auto error = run_expect_error(test_client
          .Invoke(
              &TestService::Stub::AsyncSum,
              Throw(std::runtime_error("test_error"))));
      CHECK(ExceptionMessage(error) == "test_error");
    }

    SECTION("stream failed after one message") {
      auto error = run_expect_error(test_client
          .Invoke(
              &TestService::Stub::AsyncSum,
              Concat(
                  Just(MakeTestRequest(0)),
                  Throw(std::runtime_error("test_error")))));
      CHECK(ExceptionMessage(error) == "test_error");
    }

    SECTION("two message") {
      run(Pipe(
          test_client.Invoke(
              &TestService::Stub::AsyncSum,
              Just(MakeTestRequest(13), MakeTestRequest(7))),
          Map([](Flatbuffer<TestResponse> response) {
            CHECK(response->data() == 20);
            return "ignored";
          }),
          Count(),
          Map([](int count) {
            CHECK(count == 1);
            return "ignored";
          })));
    }

    SECTION("no messages then fail") {
      auto error = run_expect_error(Pipe(
          test_client.Invoke(
              &TestService::Stub::AsyncImmediatelyFailingSum,
              Empty()),
          Map([](Flatbuffer<TestResponse> response) {
            CHECK(!"should not happen");
            return "unused";
          })));
      CHECK(ExceptionMessage(error) == "sum_fail");
    }

    SECTION("message then immediately fail") {
      auto error = run_expect_error(Pipe(
          test_client.Invoke(
              &TestService::Stub::AsyncImmediatelyFailingSum,
              Just(MakeTestRequest(1337))),
          Map([](Flatbuffer<TestResponse> response) {
            CHECK(!"should not happen");
            return "unused";
          })));
      CHECK(ExceptionMessage(error) == "sum_fail");
    }

    SECTION("fail on first message") {
      auto error = run_expect_error(Pipe(
          test_client.Invoke(
              &TestService::Stub::AsyncFailingSum,
              Just(MakeTestRequest(-1))),
          Map([](Flatbuffer<TestResponse> response) {
            CHECK(!"should not happen");
            return "unused";
          })));
      CHECK(ExceptionMessage(error) == "sum_fail");
    }

    SECTION("fail on second message") {
      auto error = run_expect_error(Pipe(
          test_client .Invoke(
              &TestService::Stub::AsyncFailingSum,
              Just(MakeTestRequest(0), MakeTestRequest(-1))),
          Map([](Flatbuffer<TestResponse> response) {
            CHECK(!"should not happen");
            return "unused";
          })));
      CHECK(ExceptionMessage(error) == "sum_fail");
    }

    SECTION("fail because of no response") {
      auto error = run_expect_error(Pipe(
          test_client .Invoke(
              &TestService::Stub::AsyncClientStreamNoResponse,
              Just(MakeTestRequest(0))),
          Map([](Flatbuffer<TestResponse> response) {
            CHECK(!"should not happen");
            return "unused";
          })));
      CHECK(ExceptionMessage(error) == "No response");
    }

    SECTION("fail because of two responses") {
      auto error = run_expect_error(Pipe(
          test_client .Invoke(
              &TestService::Stub::AsyncClientStreamTwoResponses,
              Just(MakeTestRequest(0))),
          Map([](Flatbuffer<TestResponse> response) {
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
          Map([](Flatbuffer<TestResponse> response) {
            CHECK(response->data() == 20);
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
          Map([](Flatbuffer<TestResponse> response) {
            CHECK(response->data() == 12);
            return "ignored";
          }),
          Count(),
          Map([](int count) {
            CHECK(count == 1);
            return "ignored";
          }));

      run(Merge<const char *>(call_0, call_1));
    }

    SECTION("same call twice") {
      auto call = Pipe(
          test_client.Invoke(
              &TestService::Stub::AsyncSum,
              Just(MakeTestRequest(13), MakeTestRequest(7))),
          Map([](Flatbuffer<TestResponse> response) {
            CHECK(response->data() == 20);
            return "ignored";
          }),
          Count(),
          Map([](int count) {
            CHECK(count == 1);
            return "ignored";
          }));

      run(Merge<const char *>(call, call));
    }
  }

  SECTION("bidi streaming") {
    SECTION("no messages") {
      run(Pipe(
          test_client.Invoke(
              &TestService::Stub::AsyncCumulativeSum,
              Empty()),
          Count(),
          Map([](int count) {
            CHECK(count == 0);
            return "ignored";
          })));
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
          run_expect_timeout(publisher, ElementCount(i));
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

      SECTION("make call that never requests elements") {
        auto publisher = Pipe(
            test_client.Invoke(
                &TestService::Stub::AsyncBidiStreamRequestZero,
                Just(MakeTestRequest(432))),
            Map([](Flatbuffer<TestResponse> response) {
              CHECK(!"should not be invoked");
              return "ignored";
            }));
        run_expect_timeout(publisher, ElementCount::Unbounded());
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
        run_expect_timeout(publisher, ElementCount::Unbounded());

        CHECK(hang_on_seen_elements == 2);
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
        run_expect_timeout(publisher, ElementCount::Unbounded());

        CHECK(hang_on_seen_elements == 3);
      }
    }

    SECTION("one message") {
      run(Pipe(
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
      auto error = run_expect_error(
          test_client.Invoke(
              &TestService::Stub::AsyncCumulativeSum,
              Throw(std::runtime_error("test_error"))));
      CHECK(ExceptionMessage(error) == "test_error");
    }

    SECTION("stream failed after one message") {
      auto error = run_expect_error(test_client
          .Invoke(
              &TestService::Stub::AsyncCumulativeSum,
              Concat(
                  Just(MakeTestRequest(0)),
                  Throw(std::runtime_error("test_error")))));
      CHECK(ExceptionMessage(error) == "test_error");
    }

    SECTION("two message") {
      run(Pipe(
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
      auto error = run_expect_error(Pipe(
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
      auto error = run_expect_error(Pipe(
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
      auto error = run_expect_error(Pipe(
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
      auto error = run_expect_error(Pipe(
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

      run(Merge<const char *>(call_0, call_1));
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

      run(Merge<const char *>(call, call));
    }
  }

  server.Shutdown(std::chrono::system_clock::now());
  server_thread.join();
}

}  // namespace shk
