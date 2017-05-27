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

#include <rxcpp/rx.hpp>

#include "rx_grpc.h"
#include "rx_grpc_flatbuffers.h"

#include "rxgrpctest.grpc.fb.h"

using namespace RxGrpcTest;

namespace shk {
namespace {

Flatbuffer<TestRequest> makeTestRequest(int data) {
  flatbuffers::FlatBufferBuilder fbb;
  auto test_request = CreateTestRequest(fbb, data);
  fbb.Finish(test_request);
  return Flatbuffer<TestRequest>::fromBuilder(&fbb);
}

Flatbuffer<TestResponse> makeTestResponse(int data) {
  flatbuffers::FlatBufferBuilder fbb;
  auto test_response = CreateTestResponse(fbb, data);
  fbb.Finish(test_response);
  return Flatbuffer<TestResponse>::fromBuilder(&fbb);
}

auto doubleHandler(Flatbuffer<TestRequest> request) {
  return rxcpp::observable<>::just<Flatbuffer<TestResponse>>(
      makeTestResponse(request->data() * 2));
}

auto repeatHandler(Flatbuffer<TestRequest> request) {
  int count = request->data();
  if (count == 0) {
    return rxcpp::observable<>::empty<Flatbuffer<TestResponse>>()
        .as_dynamic();
  } else {
    return rxcpp::observable<>::range(1, count)
        .map(&makeTestResponse)
        .as_dynamic();
  }
}

auto sumHandler(rxcpp::observable<Flatbuffer<TestRequest>> requests) {
  return requests
    .map([](Flatbuffer<TestRequest> request) {
      return request->data();
    })
    .start_with(0)  // To support empty input
    .sum()
    .map(makeTestResponse);
}

auto cumulativeSumHandler(rxcpp::observable<Flatbuffer<TestRequest>> requests) {
  return requests
    .map([](Flatbuffer<TestRequest> request) {
      return request->data();
    })
    .scan(0, [](int x, int y) { return x + y; })
    .map(makeTestResponse);
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
          &TestService::AsyncService::RequestRepeat,
          &repeatHandler)
      .registerMethod<FlatbufferRefTransform>(
          &TestService::AsyncService::RequestSum,
          &sumHandler)
      .registerMethod<FlatbufferRefTransform>(
          &TestService::AsyncService::RequestCumulativeSum,
          &cumulativeSumHandler);

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
              runloop.shutdown();
              CHECK(!"request should not fail");
              printf("Got exception: %s\n", exceptionMessage(error).c_str());
            },
            [&runloop]() {
              runloop.shutdown();
            });

    runloop.run();
  };

  const auto run_expect_error = [&](const auto &observable) {
    std::exception_ptr captured_error;
    observable
        .subscribe(
            [](auto) {
            },
            [&runloop, &captured_error](std::exception_ptr error) {
              runloop.shutdown();
              captured_error = error;
            },
            [&runloop]() {
              CHECK(!"request should fail");
              runloop.shutdown();
            });

    runloop.run();

    REQUIRE(captured_error);
    return captured_error;
  };

  SECTION("no streaming") {
    SECTION("direct") {
      run(test_client
          .invoke(
              &TestService::Stub::AsyncDouble, makeTestRequest(123))
          .map(
              [](Flatbuffer<TestResponse> response) {
                CHECK(response->data() == 123 * 2);
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
              [](Flatbuffer<TestResponse> response) {
                CHECK(response->data() == 123 * 2);
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
                    Flatbuffer<TestResponse>,
                    Flatbuffer<TestResponse>> responses) {
                CHECK(std::get<0>(responses)->data() == 123 * 2);
                CHECK(std::get<1>(responses)->data() == 321 * 2);
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
                    Flatbuffer<TestResponse>,
                    Flatbuffer<TestResponse>> responses) {
                CHECK(std::get<0>(responses)->data() == 123 * 2);
                CHECK(std::get<1>(responses)->data() == 123 * 2);
                return "ignored";
              }));
    }
  }

  SECTION("server streaming") {
    SECTION("no responses") {
      run(test_client
          .invoke(
              &TestService::Stub::AsyncRepeat, makeTestRequest(0))
          .map(
              [](Flatbuffer<TestResponse> response) {
                // Should never be called; this should be a stream that ends
                // without any values
                CHECK(false);
                return "ignored";
              }));
    }

    SECTION("one response") {
      run(test_client
          .invoke(&TestService::Stub::AsyncRepeat, makeTestRequest(1))
          .map([](Flatbuffer<TestResponse> response) {
            CHECK(response->data() == 1);
            return "ignored";
          })
          .count()
          .map([](int count) {
            CHECK(count == 1);
            return "ignored";
          }));
    }

    SECTION("two responses") {
      auto responses = test_client.invoke(
          &TestService::Stub::AsyncRepeat, makeTestRequest(2));

      auto check_count = responses
          .count()
          .map([](int count) {
            CHECK(count == 2);
            return "ignored";
          });

      auto check_sum = responses
          .map([](Flatbuffer<TestResponse> response) {
            return response->data();
          })
          .sum()
          .map([](int sum) {
            CHECK(sum == 3);
            return "ignored";
          });

      run(check_count.zip(check_sum));
    }

    SECTION("two calls") {
      auto responses_1 = test_client
          .invoke(
              &TestService::Stub::AsyncRepeat, makeTestRequest(2))
          .map([](Flatbuffer<TestResponse> response) {
            return response->data();
          })
          .sum()
          .map([](int sum) {
            CHECK(sum == 3);
            return "ignored";
          });

      auto responses_2 = test_client
          .invoke(
              &TestService::Stub::AsyncRepeat, makeTestRequest(3))
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
  }

  SECTION("client streaming") {
    SECTION("no messages") {
      run(test_client
          .invoke(
              &TestService::Stub::AsyncSum,
              rxcpp::observable<>::empty<Flatbuffer<TestRequest>>())
          .map(
              [](Flatbuffer<TestResponse> response) {
                CHECK(response->data() == 0);
                return "ignored";
              })
          .count()
          .map([](int count) {
            CHECK(count == 1);
            return "ignored";
          }));
    }

    SECTION("one message") {
      run(test_client
          .invoke(
              &TestService::Stub::AsyncSum,
              rxcpp::observable<>::just<Flatbuffer<TestRequest>>(
                  makeTestRequest(1337)))
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
          .invoke(
              &TestService::Stub::AsyncSum,
              rxcpp::observable<>::error<Flatbuffer<TestRequest>>(
                  std::runtime_error("test_error"))));
      CHECK(exceptionMessage(error) == "test_error");
    }

    SECTION("stream failed after one message") {
      auto error = run_expect_error(test_client
          .invoke(
              &TestService::Stub::AsyncSum,
              rxcpp::observable<>
                  ::error<Flatbuffer<TestRequest>>(
                      std::runtime_error("test_error"))
                  .start_with(makeTestRequest(0))));
      CHECK(exceptionMessage(error) == "test_error");
    }

    SECTION("two message") {
      run(test_client
          .invoke(
              &TestService::Stub::AsyncSum,
              rxcpp::observable<>::from<Flatbuffer<TestRequest>>(
                  makeTestRequest(13), makeTestRequest(7)))
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

    SECTION("two calls") {
      auto call_0 = test_client
          .invoke(
              &TestService::Stub::AsyncSum,
              rxcpp::observable<>::from<Flatbuffer<TestRequest>>(
                  makeTestRequest(13), makeTestRequest(7)))
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
          .invoke(
              &TestService::Stub::AsyncSum,
              rxcpp::observable<>::from<Flatbuffer<TestRequest>>(
                  makeTestRequest(10), makeTestRequest(2)))
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
          .invoke(
              &TestService::Stub::AsyncSum,
              rxcpp::observable<>::from<Flatbuffer<TestRequest>>(
                  makeTestRequest(13), makeTestRequest(7)))
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
  }


  SECTION("bidi streaming") {
    SECTION("no messages") {
      run(test_client
          .invoke(
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
          .invoke(
              &TestService::Stub::AsyncCumulativeSum,
              rxcpp::observable<>::just<Flatbuffer<TestRequest>>(
                  makeTestRequest(1337)))
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
          .invoke(
              &TestService::Stub::AsyncCumulativeSum,
              rxcpp::observable<>::error<Flatbuffer<TestRequest>>(
                  std::runtime_error("test_error"))));
      CHECK(exceptionMessage(error) == "test_error");
    }

    SECTION("two message") {
      run(test_client
          .invoke(
              &TestService::Stub::AsyncCumulativeSum,
              rxcpp::observable<>::from<Flatbuffer<TestRequest>>(
                  makeTestRequest(10), makeTestRequest(20)))
          .map([](Flatbuffer<TestResponse> response) {
            return response->data();
          })
          .sum()
          .map([](int sum) {
            CHECK(sum == 40); // (10) + (10 + 20)
            return "ignored";
          }));
    }

    SECTION("two calls") {
      auto call_0 = test_client
          .invoke(
              &TestService::Stub::AsyncCumulativeSum,
              rxcpp::observable<>::from<Flatbuffer<TestRequest>>(
                  makeTestRequest(10), makeTestRequest(20)))
          .map([](Flatbuffer<TestResponse> response) {
            return response->data();
          })
          .sum()
          .map([](int sum) {
            CHECK(sum == 40); // (10) + (10 + 20)
            return "ignored";
          });

      auto call_1 = test_client
          .invoke(
              &TestService::Stub::AsyncCumulativeSum,
              rxcpp::observable<>::from<Flatbuffer<TestRequest>>(
                  makeTestRequest(1), makeTestRequest(2)))
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
          .invoke(
              &TestService::Stub::AsyncCumulativeSum,
              rxcpp::observable<>::from<Flatbuffer<TestRequest>>(
                  makeTestRequest(10), makeTestRequest(20)))
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

  server.shutdown();
  server_thread.join();
}

}  // namespace shk
