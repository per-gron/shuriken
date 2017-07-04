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

#include <rs/backreference.h>
#include <rs/element_count.h>
#include <rs/publisher.h>
#include <rs/subscriber.h>
#include <rs/subscription.h>

namespace shk {
namespace detail {

template <typename InnerSubscriberType, typename Predicate>
class FilterSubscriber : public SubscriberBase {
 public:
  FilterSubscriber(
      InnerSubscriberType &&inner_subscriber,
      const Predicate &predicate)
      : inner_subscriber_(std::move(inner_subscriber)),
        predicate_(predicate) {}

  void TakeSubscription(Backreference<Subscription> &&subscription) {
    subscription_ = std::move(subscription);
  }

  template <typename T, class = RequireRvalue<T>>
  void OnNext(T &&t) {
    if (failed_) {
      return;
    }

    bool predicate_match = false;
    try {
      predicate_match = predicate_(static_cast<const T &>(t));
    } catch (...) {
      if (subscription_) {
        // TODO(peck): It's wrong that subscription_ can be null here; when that
        // happens we still actually need to be able to cancel the subscription.
        //
        // I think one approach to fix this is to change so that destroying the
        // Subscription implies cancellation.
        subscription_->Cancel();
      }
      failed_ = true;
      inner_subscriber_.OnError(std::current_exception());
    }
    if (!failed_) { // If predicate_ threw, failed_ could be true here
      if (predicate_match) {
        inner_subscriber_.OnNext(std::forward<T>(t));
      } else {
        if (subscription_) {
          // TODO(peck): It's wrong that subscription_ can be null here; when that
          // happens we still actually need to be able to cancel the subscription.
          //
          // I think one approach to fix this is to change so that destroying the
          // Subscription implies cancellation.
          subscription_->Request(ElementCount(1));
        }
      }
    }
  }

  void OnError(std::exception_ptr &&error) {
    if (!failed_) {
      inner_subscriber_.OnError(std::move(error));
    }
  }

  void OnComplete() {
    if (!failed_) {
      inner_subscriber_.OnComplete();
    }
  }

 private:
  bool failed_ = false;
  InnerSubscriberType inner_subscriber_;
  Backreference<Subscription> subscription_;
  Predicate predicate_;
};

}  // namespace detail

/**
 * Filter is like the functional filter operator that operates on a Publisher.
 */
template <typename Predicate>
auto Filter(Predicate &&predicate) {
  // Return an operator (it takes a Publisher and returns a Publisher)
  return [predicate = std::forward<Predicate>(predicate)](auto source) {
    // Return a Publisher
    return MakePublisher([predicate, source = std::move(source)](
        auto &&subscriber) {
      using FilterSubscriberT = detail::FilterSubscriber<
          typename std::decay<decltype(subscriber)>::type,
          typename std::decay<Predicate>::type>;

      Backreference<FilterSubscriberT> filter_ref;
      auto filter_subscriber = WithBackreference(
          &filter_ref,
          FilterSubscriberT(
              std::forward<decltype(subscriber)>(subscriber),
              predicate));

      Backreference<Subscription> sub_ref;
      auto sub = WithBackreference(
          &sub_ref,
          Subscription(source.Subscribe(std::move(filter_subscriber))));

      if (filter_ref) {  // TODO(peck): Test what happens if it's is empty
        filter_ref->TakeSubscription(std::move(sub_ref));
      }

      return sub;
    });
  };
}

}  // namespace shk
