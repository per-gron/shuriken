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

#include <thread>

#include <rs/concat.h>
#include <rs/count.h>
#include <rs/empty.h>
#include <rs/just.h>
#include <rs/map.h>
#include <rs/merge.h>
#include <rs/pipe.h>
#include <rs/range.h>
#include <rs/sum.h>
#include <rs/throw.h>
#include <rs/unpack.h>
#include <rs/zip.h>
#include <rs-grpc/rs_grpc.h>
#include <rs-grpc/rs_grpc_flatbuffers.h>

#include "rsgrpctest.grpc.fb.h"

using namespace RsGrpcTest;

namespace shk {
namespace {

Flatbuffer<TestRequest> MakeTestRequest(int data) {
  flatbuffers::FlatBufferBuilder fbb;
  auto test_request = CreateTestRequest(fbb, data);
  fbb.Finish(test_request);
  return Flatbuffer<TestRequest>::fromBuilder(&fbb);
}

Flatbuffer<TestResponse> MakeTestResponse(int data) {
  flatbuffers::FlatBufferBuilder fbb;
  auto test_response = CreateTestResponse(fbb, data);
  fbb.Finish(test_response);
  return Flatbuffer<TestResponse>::fromBuilder(&fbb);
}

auto DoubleHandler(Flatbuffer<TestRequest> request) {
  return Just(MakeTestResponse(request->data() * 2));
}

auto UnaryFailHandler(Flatbuffer<TestRequest> request) {
  return Throw(std::make_exception_ptr(
      std::runtime_error("unary_fail")));
}

auto UnaryNoResponseHandler(Flatbuffer<TestRequest> request) {
  return Empty();
}

auto UnaryTwoResponsesHandler(Flatbuffer<TestRequest> request) {
  return Just(
      MakeTestResponse(1),
      MakeTestResponse(2));
}

auto RepeatHandler(Flatbuffer<TestRequest> request) {
  int count = request->data();
  return PipeWith(
      Range(1, count),
      Map(&MakeTestResponse));
}

auto RepeatThenFailHandler(Flatbuffer<TestRequest> request) {
  return Concat(
      RepeatHandler(request),
      Throw(std::make_exception_ptr(std::runtime_error("repeat_fail"))));
}

auto SumHandler(Publisher<Flatbuffer<TestRequest>> requests) {
  return PipeWith(
      std::move(requests),  // TODO(peck): Why is this move necessary?
      Map([](Flatbuffer<TestRequest> request) {
        return request->data();
      }),
      Sum(),
      Map(MakeTestResponse));
}

#if 0  // TODO(peck)
auto ImmediatelyFailingSumHandler(
    rxcpp::observable<Flatbuffer<TestRequest>> requests) {
  // Hack: unless requests is subscribed to, nothing happens. Would be nice to
  // fix this.
  requests.subscribe([](auto) {});

  return rxcpp::observable<>::error<Flatbuffer<TestResponse>>(
      std::runtime_error("sum_fail"));
}

auto FailingSumHandler(rxcpp::observable<Flatbuffer<TestRequest>> requests) {
  return SumHandler(requests.map([](Flatbuffer<TestRequest> request) {
    if (request->data() == -1) {
      throw std::runtime_error("sum_fail");
    }
    return request;
  }));
}

auto ClientStreamNoResponseHandler(
    rxcpp::observable<Flatbuffer<TestRequest>> requests) {
  // Hack: unless requests is subscribed to, nothing happens. Would be nice to
  // fix this.
  requests.subscribe([](auto) {});

  return rxcpp::observable<>::empty<Flatbuffer<TestResponse>>();
}

auto ClientStreamTwoResponsesHandler(
    rxcpp::observable<Flatbuffer<TestRequest>> requests) {
  // Hack: unless requests is subscribed to, nothing happens. Would be nice to
  // fix this.
  requests.subscribe([](auto) {});

  return rxcpp::observable<>::from<Flatbuffer<TestResponse>>(
      MakeTestResponse(1),
      MakeTestResponse(2));
}

auto CumulativeSumHandler(rxcpp::observable<Flatbuffer<TestRequest>> requests) {
  return requests
    .map([](Flatbuffer<TestRequest> request) {
      return request->data();
    })
    .scan(0, [](int x, int y) { return x + y; })
    .map(MakeTestResponse);
}

auto ImmediatelyFailingCumulativeSumHandler(
    rxcpp::observable<Flatbuffer<TestRequest>> requests) {
  // Hack: unless requests is subscribed to, nothing happens. Would be nice to
  // fix this.
  requests.subscribe([](auto) {});

  return rxcpp::observable<>::error<Flatbuffer<TestResponse>>(
      std::runtime_error("cumulative_sum_fail"));
}

auto FailingCumulativeSumHandler(
    rxcpp::observable<Flatbuffer<TestRequest>> requests) {
  return CumulativeSumHandler(requests.map([](Flatbuffer<TestRequest> request) {
    if (request->data() == -1) {
      throw std::runtime_error("cumulative_sum_fail");
    }
    return request;
  }));
}

#endif
}  // anonymous namespace

TEST_CASE("RsGrpc") {
  auto server_address = "unix:rx_grpc_test.socket";

  RsGrpcServer::Builder server_builder;
  server_builder.GrpcServerBuilder()
      .AddListeningPort(server_address, grpc::InsecureServerCredentials());

  server_builder.RegisterService<TestService::AsyncService>()
      .RegisterMethod<FlatbufferRefTransform>(
          &TestService::AsyncService::RequestDouble,
           DoubleHandler)
      .RegisterMethod<FlatbufferRefTransform>(
          &TestService::AsyncService::RequestUnaryFail,
          &UnaryFailHandler)
      .RegisterMethod<FlatbufferRefTransform>(
          &TestService::AsyncService::RequestUnaryNoResponse,
          &UnaryNoResponseHandler)
      .RegisterMethod<FlatbufferRefTransform>(
          &TestService::AsyncService::RequestUnaryTwoResponses,
          &UnaryTwoResponsesHandler)
      .RegisterMethod<FlatbufferRefTransform>(
          &TestService::AsyncService::RequestRepeat,
          &RepeatHandler)
      .RegisterMethod<FlatbufferRefTransform>(
          &TestService::AsyncService::RequestRepeatThenFail,
          &RepeatThenFailHandler)
      .RegisterMethod<FlatbufferRefTransform>(
          &TestService::AsyncService::RequestSum,
          &SumHandler)
  ;
#if 0  // TODO(peck)
      .RegisterMethod<FlatbufferRefTransform>(
          &TestService::AsyncService::RequestImmediatelyFailingSum,
          &ImmediatelyFailingSumHandler)
      .RegisterMethod<FlatbufferRefTransform>(
          &TestService::AsyncService::RequestFailingSum,
          &FailingSumHandler)
      .RegisterMethod<FlatbufferRefTransform>(
          &TestService::AsyncService::RequestClientStreamNoResponse,
          &ClientStreamNoResponseHandler)
      .RegisterMethod<FlatbufferRefTransform>(
          &TestService::AsyncService::RequestClientStreamTwoResponses,
          &ClientStreamTwoResponsesHandler)
      .RegisterMethod<FlatbufferRefTransform>(
          &TestService::AsyncService::RequestCumulativeSum,
          &CumulativeSumHandler)
      .RegisterMethod<FlatbufferRefTransform>(
          &TestService::AsyncService::RequestImmediatelyFailingCumulativeSum,
          &ImmediatelyFailingCumulativeSumHandler)
      .RegisterMethod<FlatbufferRefTransform>(
          &TestService::AsyncService::RequestFailingCumulativeSum,
          &FailingCumulativeSumHandler);
#endif

  RsGrpcClient runloop;

  auto channel = grpc::CreateChannel(
      server_address, grpc::InsecureChannelCredentials());

  auto test_client = runloop.MakeClient<FlatbufferRefTransform>(
      TestService::NewStub(channel));

  auto server = server_builder.BuildAndStart();
  std::thread server_thread([&] { server.Run(); });

  const auto run = [&](const auto &publisher) {
    auto subscription = publisher
        .Subscribe(MakeSubscriber(
            [](auto &&) {
              // Ignore OnNext
            },
            [&runloop](std::exception_ptr error) {
              runloop.Shutdown();
              CHECK(!"request should not fail");
              printf("Got exception: %s\n", exceptionMessage(error).c_str());
            },
            [&runloop]() {
              runloop.Shutdown();
            }));
    subscription.Request(ElementCount::Infinite());

    runloop.Run();
  };

  const auto run_expect_error = [&](const auto &publisher) {
    std::exception_ptr captured_error;
    auto subscription = publisher
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
            }));
    subscription.Request(ElementCount::Infinite());

    runloop.Run();

    REQUIRE(captured_error);
    return captured_error;
  };

  // TODO(peck): Test what happens when calling unimplemented endpoint. I think
  // right now it just waits forever, which is not nice at all.

  SECTION("unary rpc") {
    SECTION("direct") {
      run(PipeWith(
          test_client.Invoke(
              &TestService::Stub::AsyncDouble, MakeTestRequest(123)),
          Map([](Flatbuffer<TestResponse> response) {
            CHECK(response->data() == 123 * 2);
            return "ignored";
          })));
    }

    SECTION("failed rpc") {
      auto error = run_expect_error(PipeWith(
          test_client
              .Invoke(&TestService::Stub::AsyncUnaryFail, MakeTestRequest(0)),
          Map([](Flatbuffer<TestResponse> response) {
            CHECK(!"should not happen");
            return "unused";
          })));
      CHECK(exceptionMessage(error) == "unary_fail");
    }

    SECTION("failed rpc because of no response") {
      auto error = run_expect_error(PipeWith(
          test_client.Invoke(
              &TestService::Stub::AsyncUnaryNoResponse, MakeTestRequest(0)),
          Map([](Flatbuffer<TestResponse> response) {
            CHECK(!"should not happen");
            return "unused";
          })));
      CHECK(exceptionMessage(error) == "No response");
    }

    SECTION("failed rpc because of two responses") {
      auto error = run_expect_error(PipeWith(
          test_client.Invoke(
              &TestService::Stub::AsyncUnaryTwoResponses,
              MakeTestRequest(0)),
          Map([](Flatbuffer<TestResponse> response) {
            CHECK(!"should not happen");
            return "unused";
          })));
      CHECK(exceptionMessage(error) == "Too many responses");
    }

    SECTION("delayed") {
      // This test can break if invoke doesn't take ownership of the request for
      // example.
      auto call = PipeWith(
          test_client.Invoke(
              &TestService::Stub::AsyncDouble, MakeTestRequest(123)),
          Map([](Flatbuffer<TestResponse> response) {
            CHECK(response->data() == 123 * 2);
            return "ignored";
          }));
      run(call);
    }

#if 0  // TODO(peck)
    SECTION("two calls") {
      auto call_a = test_client.Invoke(
          &TestService::Stub::AsyncDouble, MakeTestRequest(123));
      auto call_b = test_client.Invoke(
          &TestService::Stub::AsyncDouble, MakeTestRequest(321));
      run(PipeWith(
          Zip<std::tuple<Flatbuffer<TestResponse>, Flatbuffer<TestResponse>>>(
              call_a, call_b),
          Map(Unpack([](
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
      run(PipeWith(
          Zip<std::tuple<Flatbuffer<TestResponse>, Flatbuffer<TestResponse>>>(
              call, call),
          Map(Unpack([](
              Flatbuffer<TestResponse> a,
              Flatbuffer<TestResponse> b) {
            CHECK(a->data() == 123 * 2);
            CHECK(b->data() == 123 * 2);
            return "ignored";
          }))));
    }
#endif
  }

  SECTION("server streaming") {
    SECTION("no responses") {
      run(PipeWith(
          test_client.Invoke(
              &TestService::Stub::AsyncRepeat, MakeTestRequest(0)),
          Map([](Flatbuffer<TestResponse> &&response) {
            // Should never be called; this should be a stream that ends
            // without any values
            CHECK(false);
            return "ignored";
          })));
    }

    SECTION("one response") {
      run(PipeWith(
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

#if 0  // TODO(peck)
    SECTION("two responses") {
      auto responses = test_client.Invoke(
          &TestService::Stub::AsyncRepeat, MakeTestRequest(2));

      auto check_count = PipeWith(
          responses,
          Count(),
          Map([](int count) {
            CHECK(count == 2);
            return "ignored";
          }));

      auto check_sum = PipeWith(
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
#endif

    SECTION("no responses then fail") {
      auto error = run_expect_error(PipeWith(
          test_client.Invoke(
              &TestService::Stub::AsyncRepeatThenFail, MakeTestRequest(0)),
          Map([](Flatbuffer<TestResponse> response) {
            CHECK(!"should not happen");
            return "unused";
          })));
      CHECK(exceptionMessage(error) == "repeat_fail");
    }

    SECTION("one response then fail") {
      int count = 0;
      auto error = run_expect_error(PipeWith(
          test_client.Invoke(
              &TestService::Stub::AsyncRepeatThenFail, MakeTestRequest(1)),
          Map([&count](Flatbuffer<TestResponse> response) {
            count++;
            return "unused";
          })));
      CHECK(exceptionMessage(error) == "repeat_fail");
      CHECK(count == 1);
    }

    SECTION("two responses then fail") {
      int count = 0;
      auto error = run_expect_error(PipeWith(
          test_client.Invoke(
              &TestService::Stub::AsyncRepeatThenFail, MakeTestRequest(2)),
          Map([&count](Flatbuffer<TestResponse> response) {
            count++;
            return "unused";
          })));
      CHECK(exceptionMessage(error) == "repeat_fail");
      CHECK(count == 2);
    }

#if 0  // TODO(peck)
    SECTION("two calls") {
      auto responses_1 = test_client
          .Invoke(
              &TestService::Stub::AsyncRepeat, MakeTestRequest(2))
          .map([](Flatbuffer<TestResponse> response) {
            return response->data();
          })
          .sum()
          .map([](int sum) {
            CHECK(sum == 3);
            return "ignored";
          });

      auto responses_2 = test_client
          .Invoke(
              &TestService::Stub::AsyncRepeat, MakeTestRequest(3))
          .map([](Flatbuffer<TestResponse> response) {
            return response->data();
          })
          .sum()
          .map([](int sum) {
            CHECK(sum == 6);
            return "ignored";
          });

      run(responses_1.zip(responses_2));
    }
#endif
  }

  SECTION("client streaming") {
#if 0  // TODO(peck)
    SECTION("no messages") {
      run(PipeWith(
          test_client.Invoke(
              &TestService::Stub::AsyncSum,
              // TODO(peck): This type erasure should not be needed
              Publisher<Flatbuffer<TestRequest>>(Empty())),
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

    SECTION("one message") {
      run(test_client
          .Invoke(
              &TestService::Stub::AsyncSum,
              rxcpp::observable<>::just<Flatbuffer<TestRequest>>(
                  MakeTestRequest(1337)))
          .map(
              [](Flatbuffer<TestResponse> response) {
                CHECK(response->data() == 1337);
                return "ignored";
              })
          .count()
          .map([](int count) {
            CHECK(count == 1);
            return "ignored";
          }));
    }

    SECTION("immediately failed stream") {
      auto error = run_expect_error(test_client
          .Invoke(
              &TestService::Stub::AsyncSum,
              rxcpp::observable<>::error<Flatbuffer<TestRequest>>(
                  std::runtime_error("test_error"))));
      CHECK(exceptionMessage(error) == "test_error");
    }

    SECTION("stream failed after one message") {
      auto error = run_expect_error(test_client
          .Invoke(
              &TestService::Stub::AsyncSum,
              rxcpp::observable<>
                  ::error<Flatbuffer<TestRequest>>(
                      std::runtime_error("test_error"))
                  .start_with(MakeTestRequest(0))));
      CHECK(exceptionMessage(error) == "test_error");
    }

    SECTION("two message") {
      run(test_client
          .Invoke(
              &TestService::Stub::AsyncSum,
              rxcpp::observable<>::from<Flatbuffer<TestRequest>>(
                  MakeTestRequest(13), MakeTestRequest(7)))
          .map(
              [](Flatbuffer<TestResponse> response) {
                CHECK(response->data() == 20);
                return "ignored";
              })
          .count()
          .map([](int count) {
            CHECK(count == 1);
            return "ignored";
          }));
    }

    SECTION("no messages then fail") {
      auto error = run_expect_error(test_client
          .Invoke(
              &TestService::Stub::AsyncImmediatelyFailingSum,
              rxcpp::observable<>::empty<Flatbuffer<TestRequest>>())
          .map([](Flatbuffer<TestResponse> response) {
            CHECK(!"should not happen");
            return "unused";
          }));
      CHECK(exceptionMessage(error) == "sum_fail");
    }

    SECTION("message then immediately fail") {
      auto error = run_expect_error(test_client
          .Invoke(
              &TestService::Stub::AsyncImmediatelyFailingSum,
              rxcpp::observable<>::just<Flatbuffer<TestRequest>>(
                  MakeTestRequest(1337)))
          .map([](Flatbuffer<TestResponse> response) {
            CHECK(!"should not happen");
            return "unused";
          }));
      CHECK(exceptionMessage(error) == "sum_fail");
    }

    SECTION("fail on first message") {
      auto error = run_expect_error(test_client
          .Invoke(
              &TestService::Stub::AsyncFailingSum,
              rxcpp::observable<>::just<Flatbuffer<TestRequest>>(
                  MakeTestRequest(-1)))
          .map([](Flatbuffer<TestResponse> response) {
            CHECK(!"should not happen");
            return "unused";
          }));
      CHECK(exceptionMessage(error) == "sum_fail");
    }

    SECTION("fail on second message") {
      auto error = run_expect_error(test_client
          .Invoke(
              &TestService::Stub::AsyncFailingSum,
              rxcpp::observable<>::from<Flatbuffer<TestRequest>>(
                  MakeTestRequest(0), MakeTestRequest(-1)))
          .map([](Flatbuffer<TestResponse> response) {
            CHECK(!"should not happen");
            return "unused";
          }));
      CHECK(exceptionMessage(error) == "sum_fail");
    }

    SECTION("fail because of no response") {
      auto error = run_expect_error(test_client
          .Invoke(
              &TestService::Stub::AsyncClientStreamNoResponse,
              rxcpp::observable<>::just<Flatbuffer<TestRequest>>(
                  MakeTestRequest(0)))
          .map([](Flatbuffer<TestResponse> response) {
            CHECK(!"should not happen");
            return "unused";
          }));
      CHECK(exceptionMessage(error) == "No response");
    }

    SECTION("fail because of two responses") {
      auto error = run_expect_error(test_client
          .Invoke(
              &TestService::Stub::AsyncClientStreamTwoResponses,
              rxcpp::observable<>::just<Flatbuffer<TestRequest>>(
                  MakeTestRequest(0)))
          .map([](Flatbuffer<TestResponse> response) {
            CHECK(!"should not happen");
            return "unused";
          }));
      CHECK(exceptionMessage(error) == "Too many responses");
    }

    SECTION("two calls") {
      auto call_0 = test_client
          .Invoke(
              &TestService::Stub::AsyncSum,
              rxcpp::observable<>::from<Flatbuffer<TestRequest>>(
                  MakeTestRequest(13), MakeTestRequest(7)))
          .map(
              [](Flatbuffer<TestResponse> response) {
                CHECK(response->data() == 20);
                return "ignored";
              })
          .count()
          .map([](int count) {
            CHECK(count == 1);
            return "ignored";
          });

      auto call_1 = test_client
          .Invoke(
              &TestService::Stub::AsyncSum,
              rxcpp::observable<>::from<Flatbuffer<TestRequest>>(
                  MakeTestRequest(10), MakeTestRequest(2)))
          .map(
              [](Flatbuffer<TestResponse> response) {
                CHECK(response->data() == 12);
                return "ignored";
              })
          .count()
          .map([](int count) {
            CHECK(count == 1);
            return "ignored";
          });

      run(call_0.zip(call_1));
    }

    SECTION("same call twice") {
      auto call = test_client
          .Invoke(
              &TestService::Stub::AsyncSum,
              rxcpp::observable<>::from<Flatbuffer<TestRequest>>(
                  MakeTestRequest(13), MakeTestRequest(7)))
          .map(
              [](Flatbuffer<TestResponse> response) {
                CHECK(response->data() == 20);
                return "ignored";
              })
          .count()
          .map([](int count) {
            CHECK(count == 1);
            return "ignored";
          });

      run(call.zip(call));
    }
#endif
  }

#if 0  // TODO(peck)
  SECTION("bidi streaming") {
    SECTION("no messages") {
      run(test_client
          .Invoke(
              &TestService::Stub::AsyncCumulativeSum,
              rxcpp::observable<>::empty<Flatbuffer<TestRequest>>())
          .count()
          .map([](int count) {
            CHECK(count == 0);
            return "ignored";
          }));
    }

    SECTION("one message") {
      run(test_client
          .Invoke(
              &TestService::Stub::AsyncCumulativeSum,
              rxcpp::observable<>::just<Flatbuffer<TestRequest>>(
                  MakeTestRequest(1337)))
          .map(
              [](Flatbuffer<TestResponse> response) {
                CHECK(response->data() == 1337);
                return "ignored";
              })
          .count()
          .map([](int count) {
            CHECK(count == 1);
            return "ignored";
          }));
    }

    SECTION("immediately failed stream") {
      auto error = run_expect_error(test_client
          .Invoke(
              &TestService::Stub::AsyncCumulativeSum,
              rxcpp::observable<>::error<Flatbuffer<TestRequest>>(
                  std::runtime_error("test_error"))));
      CHECK(exceptionMessage(error) == "test_error");
    }

    SECTION("stream failed after one message") {
      auto error = run_expect_error(test_client
          .Invoke(
              &TestService::Stub::AsyncCumulativeSum,
              rxcpp::observable<>
                  ::error<Flatbuffer<TestRequest>>(
                      std::runtime_error("test_error"))
                  .start_with(MakeTestRequest(0))));
      CHECK(exceptionMessage(error) == "test_error");
    }

    SECTION("two message") {
      run(test_client
          .Invoke(
              &TestService::Stub::AsyncCumulativeSum,
              rxcpp::observable<>::from<Flatbuffer<TestRequest>>(
                  MakeTestRequest(10), MakeTestRequest(20)))
          .map([](Flatbuffer<TestResponse> response) {
            return response->data();
          })
          .sum()
          .map([](int sum) {
            CHECK(sum == 40); // (10) + (10 + 20)
            return "ignored";
          }));
    }

    SECTION("no messages then fail") {
      auto error = run_expect_error(test_client
          .Invoke(
              &TestService::Stub::AsyncImmediatelyFailingCumulativeSum,
              rxcpp::observable<>::empty<Flatbuffer<TestRequest>>())
          .map([](Flatbuffer<TestResponse> response) {
            CHECK(!"should not happen");
            return "unused";
          }));
      CHECK(exceptionMessage(error) == "cumulative_sum_fail");
    }

    SECTION("message then immediately fail") {
      auto error = run_expect_error(test_client
          .Invoke(
              &TestService::Stub::AsyncImmediatelyFailingCumulativeSum,
              rxcpp::observable<>::just<Flatbuffer<TestRequest>>(
                  MakeTestRequest(1337)))
          .map([](Flatbuffer<TestResponse> response) {
            CHECK(!"should not happen");
            return "unused";
          }));
      CHECK(exceptionMessage(error) == "cumulative_sum_fail");
    }

    SECTION("fail on first message") {
      auto error = run_expect_error(test_client
          .Invoke(
              &TestService::Stub::AsyncFailingCumulativeSum,
              rxcpp::observable<>::just<Flatbuffer<TestRequest>>(
                  MakeTestRequest(-1)))
          .map([](Flatbuffer<TestResponse> response) {
            CHECK(!"should not happen");
            return "unused";
          }));
      CHECK(exceptionMessage(error) == "cumulative_sum_fail");
    }

    SECTION("fail on second message") {
      int count = 0;
      auto error = run_expect_error(test_client
          .Invoke(
              &TestService::Stub::AsyncFailingCumulativeSum,
              rxcpp::observable<>::from<Flatbuffer<TestRequest>>(
                  MakeTestRequest(321), MakeTestRequest(-1)))
          .map([&count](Flatbuffer<TestResponse> response) {
            CHECK(response->data() == 321);
            count++;
            return "unused";
          }));
      CHECK(exceptionMessage(error) == "cumulative_sum_fail");
      CHECK(count == 1);
    }

    SECTION("two calls") {
      auto call_0 = test_client
          .Invoke(
              &TestService::Stub::AsyncCumulativeSum,
              rxcpp::observable<>::from<Flatbuffer<TestRequest>>(
                  MakeTestRequest(10), MakeTestRequest(20)))
          .map([](Flatbuffer<TestResponse> response) {
            return response->data();
          })
          .sum()
          .map([](int sum) {
            CHECK(sum == 40); // (10) + (10 + 20)
            return "ignored";
          });

      auto call_1 = test_client
          .Invoke(
              &TestService::Stub::AsyncCumulativeSum,
              rxcpp::observable<>::from<Flatbuffer<TestRequest>>(
                  MakeTestRequest(1), MakeTestRequest(2)))
          .map([](Flatbuffer<TestResponse> response) {
            return response->data();
          })
          .sum()
          .map([](int sum) {
            CHECK(sum == 4); // (1) + (1 + 2)
            return "ignored";
          });

      run(call_0.zip(call_1));
    }

    SECTION("same call twice") {
      auto call = test_client
          .Invoke(
              &TestService::Stub::AsyncCumulativeSum,
              rxcpp::observable<>::from<Flatbuffer<TestRequest>>(
                  MakeTestRequest(10), MakeTestRequest(20)))
          .map([](Flatbuffer<TestResponse> response) {
            return response->data();
          })
          .sum()
          .map([](int sum) {
            CHECK(sum == 40); // (10) + (10 + 20)
            return "ignored";
          });

      run(call.zip(call));
    }
  }
#endif

  server.Shutdown();
  server_thread.join();
}

}  // namespace shk
