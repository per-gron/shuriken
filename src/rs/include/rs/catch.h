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

#include <memory>

#include <rs/element_count.h>
#include <rs/publisher.h>
#include <rs/subscriber.h>
#include <rs/subscription.h>

namespace shk {
namespace detail {

template <typename Subscriber>
class CatchSubscription : public SubscriptionBase {
 public:
  CatchSubscription(const std::shared_ptr<Subscriber> &subscriber)
      : subscriber_(subscriber) {}

  void Request(ElementCount count) {
    subscriber_->requested_ += count;
    inner_subscription_.Request(count);
  }

  void Cancel() {
    subscriber_->cancelled_ = true;
    inner_subscription_.Cancel();
  }

 private:
  template <typename, typename> friend class CatchSubscriber;

  Subscription inner_subscription_;
  std::shared_ptr<Subscriber> subscriber_;
};

template <typename InnerSubscriberType, typename Callback>
class CatchSubscriber : public SubscriberBase {
 public:
  CatchSubscriber(
      InnerSubscriberType &&inner_subscriber,
      const Callback &callback)
      : inner_subscriber_(std::move(inner_subscriber)),
        callback_(callback) {}

  template <typename Publisher>
  void Subscribe(
      const std::shared_ptr<CatchSubscription<CatchSubscriber>> &subscription,
      const std::shared_ptr<CatchSubscriber> &me,
      Publisher &&publisher) {
    subscription_ = subscription;
    me_ = me;
    auto sub = publisher.Subscribe(MakeSubscriber(me));
    if (!has_failed_) {
      // It is possible that Subscribe causes OnError to be called before it
      // even returns. In that case, inner_subscription_ will have been set to
      // the catch subscription before Subscribe returns, and then we must not
      // overwrite inner_subscription_
      if (auto subscription = subscription_.lock()) {
        subscription->inner_subscription_ = Subscription(std::move(sub));
      }
    }
  }

  template <typename T, class = RequireRvalue<T>>
  void OnNext(T &&t) {
    --requested_;
    if (requested_ < 0) {
      cancelled_ = true;
      inner_subscriber_.OnError(std::make_exception_ptr(
          std::logic_error("Got value that was not Request-ed")));
    } else {
      inner_subscriber_.OnNext(std::forward<T>(t));
    }
  }

  void OnError(std::exception_ptr &&error) {
    if (cancelled_) {
      // Do nothing
    } else if (has_failed_) {
      inner_subscriber_.OnError(std::move(error));
    } else {
      has_failed_ = true;
      auto catch_publisher = callback_(std::move(error));
      static_assert(
          IsPublisher<decltype(catch_publisher)>,
          "Catch callback must return a Publisher");

      auto sub = catch_publisher.Subscribe(MakeSubscriber(me_.lock()));
      sub.Request(requested_);
      if (auto subscription = subscription_.lock()) {
        subscription->inner_subscription_ = Subscription(std::move(sub));
      }
    }
  }

  void OnComplete() {
    if (!cancelled_) {
      inner_subscriber_.OnComplete();
    }
  }

 private:
  template <typename> friend class CatchSubscription;

  // The number of elements that have been requested but not yet emitted.
  ElementCount requested_;
  // If the subscription has been cancelled. This is important to keep track of
  // because a cancelled subscription may fail, and in that case we don't want
  // to subscribe to the catch Publisher since that would undo the cancellation.
  bool cancelled_ = false;

  std::weak_ptr<CatchSubscription<CatchSubscriber>> subscription_;
  std::weak_ptr<CatchSubscriber> me_;
  InnerSubscriberType inner_subscriber_;
  Callback callback_;
  // The number of elements that have been requested but not yet emitted.
  bool has_failed_ = false;
};

}  // namespace detail

/**
 * Catch is an asynchronous version of a try/catch statement. It makes an
 * operator that takes a Publisher and returns a Publisher that behaves exactly
 * the same, except if it ends with an error. If so, callback is called and the
 * stream continues with the Publisher that Callback returned.
 */
template <typename Callback>
auto Catch(Callback &&callback) {
  // Return an operator (it takes a Publisher and returns a Publisher)
  return [callback = std::forward<Callback>(callback)](auto source) {
    // Return a Publisher
    return MakePublisher([callback, source = std::move(source)](
        auto &&subscriber) {
      using CatchSubscriberT = detail::CatchSubscriber<
          typename std::decay<decltype(subscriber)>::type,
          typename std::decay<Callback>::type>;

      auto catch_subscriber = std::make_shared<CatchSubscriberT>(
          std::forward<decltype(subscriber)>(subscriber),
          callback);

      auto subscription = std::make_shared<
          detail::CatchSubscription<CatchSubscriberT>>(catch_subscriber);

      catch_subscriber->Subscribe(subscription, catch_subscriber, source);

      return MakeSubscription(subscription);
    });
  };
}

}  // namespace shk
