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

#pragma once

#include <type_traits>

#include <rs/element_count.h>
#include <rs/publisher.h>
#include <rs/subscriber.h>
#include <rs/subscription.h>

namespace shk {
namespace detail {

template <typename InnerSubscriberType, typename Predicate>
class TakeWhileSubscriber : public SubscriberBase, public SubscriptionBase {
 public:
  TakeWhileSubscriber(
      InnerSubscriberType &&inner_subscriber,
      const Predicate &predicate)
      : inner_subscriber_(std::move(inner_subscriber)),
        subscription_(MakeSubscription()),
        predicate_(predicate) {}

  template <typename SubscriptionT>
  void TakeSubscription(SubscriptionT &&subscription) {
    subscription_ = Subscription(std::forward<SubscriptionT>(subscription));
  }

  template <typename T, class = RequireRvalue<T>>
  void OnNext(T &&t) {
    if (cancelled_) {
      return;
    }

    bool predicate_match = false;
    try {
      predicate_match = predicate_(static_cast<const T &>(t));
    } catch (...) {
      Cancel();
      inner_subscriber_.OnError(std::current_exception());
    }
    if (!cancelled_) { // If predicate_ threw, cancelled_ could be true here
      if (predicate_match) {
        inner_subscriber_.OnNext(std::forward<T>(t));
      } else {
        inner_subscriber_.OnComplete();
        Cancel();
      }
    }
  }

  void OnError(std::exception_ptr &&error) {
    if (!cancelled_) {
      inner_subscriber_.OnError(std::move(error));
    }
  }

  void OnComplete() {
    if (!cancelled_) {
      inner_subscriber_.OnComplete();
    }
  }

  void Request(ElementCount count) {
    subscription_.Request(count);
  }

  void Cancel() {
    subscription_.Cancel();
    cancelled_ = true;
  }

 private:
  bool cancelled_ = false;
  InnerSubscriberType inner_subscriber_;
  Subscription subscription_;
  Predicate predicate_;
};

}  // namespace detail

template <typename Predicate>
auto TakeWhile(Predicate &&predicate) {
  // Return an operator (it takes a Publisher and returns a Publisher)
  return [predicate = std::forward<Predicate>(predicate)](auto source) {
    // Return a Publisher
    return MakePublisher([predicate, source = std::move(source)](
        auto &&subscriber) {
      auto take_while_subscriber = std::make_shared<detail::TakeWhileSubscriber<
          typename std::decay<decltype(subscriber)>::type,
          typename std::decay<Predicate>::type>>(
              std::forward<decltype(subscriber)>(subscriber),
              predicate);

      take_while_subscriber->TakeSubscription(
          source.Subscribe(MakeSubscriber(take_while_subscriber)));

      return MakeSubscription(take_while_subscriber);
    });
  };
}

}  // namespace shk
