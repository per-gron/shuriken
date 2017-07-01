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

#include <rs/element_count.h>

namespace shk {
namespace detail {

template <typename InnerSubscriberType, typename CountType>
class TakeSubscriber : public SubscriberBase, public SubscriptionBase {
 public:
  TakeSubscriber(
      InnerSubscriberType &&inner_subscriber,
      const CountType &count)
      : inner_subscriber_(std::move(inner_subscriber)),
        count_(count) {}

  template <typename PublisherT>
  SharedSubscription TakeSubscription(
      const std::shared_ptr<TakeSubscriber> &me,
      PublisherT *source) {
    if (count_ == 0) {
      cancelled_ = true;
      inner_subscriber_.OnComplete();

      return SharedSubscription();
    } else {
      auto sub = SharedSubscription(source->Subscribe(MakeSubscriber(me)));
      subscription_ = WeakSubscription(sub);
      return sub;
    }
  }

  template <typename T, class = RequireRvalue<T>>
  void OnNext(T &&t) {
    if (cancelled_) {
      return;
    }

    if (count_ > 0) {
      inner_subscriber_.OnNext(std::forward<T>(t));
    }
    --count_;
    if (count_ <= 0) {
      inner_subscriber_.OnComplete();
      subscription_.Cancel();
      cancelled_ = true;
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
  }

 private:
  bool cancelled_ = false;
  InnerSubscriberType inner_subscriber_;
  WeakSubscription subscription_;
  CountType count_;
};

}  // namespace detail

template <typename CountType>
auto Take(CountType &&count) {
  // Return an operator (it takes a Publisher and returns a Publisher)
  return [count = std::forward<CountType>(count)](auto source) {
    // Return a Publisher
    return MakePublisher([count, source = std::move(source)](
        auto &&subscriber) {
      auto take_subscriber = std::make_shared<detail::TakeSubscriber<
          typename std::decay<decltype(subscriber)>::type,
          typename std::decay<CountType>::type>>(
              std::forward<decltype(subscriber)>(subscriber),
              count);

      return take_subscriber->TakeSubscription(take_subscriber, &source);
    });
  };
}

}  // namespace shk
