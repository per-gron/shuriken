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
#include <rs-grpc/server.h>

#include "rs-grpc/test/rsgrpctest.grpc.pb.h"

#if defined(__GNUC__) && (__GNUC__ >= 4)
#define RS_GPRC_USE_RESULT __attribute__ ((warn_unused_result))
#elif defined(_MSC_VER) && (_MSC_VER >= 1700)
#define RS_GPRC_USE_RESULT _Check_return_
#else
#define RS_GPRC_USE_RESULT
#endif

namespace shk {

void InitTests();

TestRequest MakeTestRequest(int data);

TestResponse MakeTestResponse(int data);

void ShutdownAllowOutstandingCall(RsGrpcServer *server);

AnyPublisher<TestRequest> MakeInfiniteRequest();

AnyPublisher<TestResponse> MakeInfiniteResponse();

template <typename Publisher>
std::exception_ptr RunExpectError(
    RsGrpcClientRunloop *runloop,
    const Publisher &publisher,
    std::function<void (Subscription &)> subscribe = nullptr) {
  std::exception_ptr captured_error;
  auto subscription = AnySubscription(publisher
      .Subscribe(MakeSubscriber(
          [](auto &&) {
            // Ignore OnNext
          },
          [runloop, &captured_error](std::exception_ptr &&error) {
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

/**
 * Returns a "tag" object that, when destroyed, pumps the remaining events on
 * the client runloop. Without it, tests may leak memory.
 */
template <typename Publisher>
RS_GPRC_USE_RESULT std::shared_ptr<void> RunExpectTimeout(
    RsGrpcClientRunloop *runloop,
    const Publisher &publisher,
    ElementCount count = ElementCount(0)) {
  auto shutting_down = std::make_shared<bool>(false);

  auto subscription = AnySubscription(publisher
      .Subscribe(MakeSubscriber(
          [](auto &&) {
            // Ignore OnNext
          },
          [shutting_down](std::exception_ptr &&error) {
            if (!*shutting_down) {
              CHECK(!"request should not fail");
            } else {
              CHECK(
                  ExceptionToStatus(error).error_code() ==
                  GRPC_STATUS_INTERNAL);
            }
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

  return std::shared_ptr<int>(nullptr, [shutting_down, runloop](void *ptr) {
    *shutting_down = true;
    runloop->Shutdown();
    runloop->Run();
  });
}

template <typename Publisher>
void Run(
    RsGrpcClientRunloop *runloop,
    const Publisher &publisher,
    std::function<void (AnySubscription &&)> subscribe = nullptr) {
  auto subscription = publisher
      .Subscribe(MakeSubscriber(
          [](auto &&) {
            // Ignore OnNext
          },
          [runloop](std::exception_ptr &&error) {
            runloop->Shutdown();
            CHECK(!"request should not fail");
            printf("Got exception: %s\n", ExceptionMessage(error).c_str());
          },
          [runloop]() {
            runloop->Shutdown();
          }));
  if (subscribe) {
    subscribe(AnySubscription(std::move(subscription)));
  } else {
    subscription.Request(ElementCount::Unbounded());
  }

  runloop->Run();
}

inline auto RequestZeroHandler(
    const CallContext &ctx, AnyPublisher<TestRequest> &&requests) {
  // The point of this test endpoint is to request some inputs, and verify that
  // it doesn't get more than that pushed to it. This endpoint never responds
  // so tests have to suceed by timing out.

  auto subscription = requests.Subscribe(MakeSubscriber(
      [](auto &&) {
        CHECK(!"no elements should be published");
      },
      [](std::exception_ptr error) {
        CHECK(!"request should not fail");
      },
      []() {
        CHECK(!"request should not complete");
      }));
  subscription.Request(ElementCount(0));

  return Never();
}

inline auto MakeHangOnZeroHandler(
    std::atomic<int> *hang_on_seen_elements,
    std::shared_ptr<AnySubscription> *hung_subscription) {
  return [hang_on_seen_elements, hung_subscription](
      const CallContext &ctx, AnyPublisher<TestRequest> requests) {
    // The point of this test endpoint is to request some inputs, and verify that
    // it doesn't get more than that pushed to it. This endpoint never responds
    // so tests have to suceed by timing out.

    bool seen_zero = false;
    std::shared_ptr<AnySubscription> sub =
        std::make_shared<AnySubscription>(MakeSubscription());
    *sub = AnySubscription(requests.Subscribe(MakeSubscriber(
        [&seen_zero, sub, hang_on_seen_elements, hung_subscription](
            TestRequest &&request) mutable {
          (*hang_on_seen_elements)++;
          CHECK(!seen_zero);
          REQUIRE(sub);
          if (request.data() == 0) {
            seen_zero = true;
            *hung_subscription = std::move(sub);
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
