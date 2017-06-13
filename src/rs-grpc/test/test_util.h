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
#include <memory>

#include <rs/never.h>
#include <rs/publisher.h>
#include <rs/subscriber.h>
#include <rs/subscription.h>
#include <flatbuffers/grpc.h>
#include <rs-grpc/server.h>

#include "rsgrpctest.grpc.fb.h"

namespace shk {

using namespace RsGrpcTest;

template <typename T>
using Flatbuffer = flatbuffers::grpc::Message<T>;

Flatbuffer<TestRequest> MakeTestRequest(int data);

Flatbuffer<TestResponse> MakeTestResponse(int data);

void ShutdownAllowOutstandingCall(RsGrpcServer *server);

Publisher<Flatbuffer<TestRequest>> MakeInfiniteRequest();

Publisher<Flatbuffer<TestResponse>> MakeInfiniteResponse();

template <typename Publisher>
std::exception_ptr RunExpectError(
    RsGrpcClient *runloop,
    const Publisher &publisher,
    std::function<void (Subscription &)> subscribe = nullptr) {
  std::exception_ptr captured_error;
  auto subscription = Subscription(publisher
      .Subscribe(MakeSubscriber(
          [](auto &&) {
            // Ignore OnNext
          },
          [runloop, &captured_error](std::exception_ptr error) {
            runloop->Shutdown();
            captured_error = error;
          },
          [runloop]() {
            CHECK(!"request should fail");
            runloop->Shutdown();
          })));
  if (subscribe) {
    subscribe(subscription);
  } else {
    subscription.Request(ElementCount::Unbounded());
  }

  runloop->Run();

  REQUIRE(captured_error);
  return captured_error;
}

template <typename Publisher>
void RunExpectTimeout(
    RsGrpcClient *runloop,
    const Publisher &publisher,
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
    auto deadline = std::chrono::system_clock::now() + 20ms;
    if (runloop->Next(deadline) == grpc::CompletionQueue::TIMEOUT) {
      break;
    }
  }
}

template <typename Publisher>
void Run(
    RsGrpcClient *runloop,
    const Publisher &publisher,
    std::function<void (Subscription &)> subscribe = nullptr) {
  auto subscription = Subscription(publisher
      .Subscribe(MakeSubscriber(
          [](auto &&) {
            // Ignore OnNext
          },
          [runloop](std::exception_ptr error) {
            runloop->Shutdown();
            CHECK(!"request should not fail");
            printf("Got exception: %s\n", ExceptionMessage(error).c_str());
          },
          [runloop]() {
            runloop->Shutdown();
          })));
  if (subscribe) {
    subscribe(subscription);
  } else {
    subscription.Request(ElementCount::Unbounded());
  }

  runloop->Run();
}

inline auto RequestZeroHandler(
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

inline auto MakeHangOnZeroHandler(
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

}  // namespace shk
