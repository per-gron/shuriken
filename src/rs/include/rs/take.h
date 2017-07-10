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

#include <rs/backreference.h>
#include <rs/element_count.h>

namespace shk {
namespace detail {

template <typename InnerSubscriberType, typename CountType>
class TakeSubscriber : public SubscriberBase {
 public:
  template <typename InnerSubscriberT>
  TakeSubscriber(
      InnerSubscriberT &&inner_subscriber,
      const CountType &count)
      : inner_subscriber_(std::forward<InnerSubscriberT>(inner_subscriber)),
        count_(count) {}

  template <typename InnerSubscriberT, typename PublisherT>
  static Backreferee<AnySubscription> Build(
      const CountType &count,
      InnerSubscriberT &&inner_subscriber,
      PublisherT *source) {
    if (count == 0) {
      inner_subscriber.OnComplete();

      Backreference<AnySubscription> ref;
      return Backreferee<AnySubscription>();
    } else {
      TakeSubscriber self(
          std::forward<InnerSubscriberT>(inner_subscriber),
          count);

      Backreference<TakeSubscriber> take_ref;
      auto backreferred_self = WithBackreference(std::move(self), &take_ref);

      auto sub = AnySubscription(
          source->Subscribe(std::move(backreferred_self)));

      return WithBackreference(
          std::move(sub),
          &take_ref->subscription_);
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
      if (subscription_) {
        // TODO(peck): It's wrong that subscription_ can be null here; when that
        // happens we still actually need to be able to cancel the subscription.
        //
        // I think one approach to fix this is to change so that destroying the
        // Subscription implies cancellation.
        subscription_->Cancel();
      }
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

 private:
  bool cancelled_ = false;
  InnerSubscriberType inner_subscriber_;
  Backreference<AnySubscription> subscription_;
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
      using TakeSubscriberT = detail::TakeSubscriber<
          typename std::decay<decltype(subscriber)>::type,
          typename std::decay<CountType>::type>;

      return TakeSubscriberT::Build(
              count,
              std::forward<decltype(subscriber)>(subscriber),
              &source);
    });
  };
}

}  // namespace shk
