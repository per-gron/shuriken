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

#include <deque>
#include <type_traits>

#include <rs/element_count.h>
#include <rs/publisher.h>
#include <rs/subscriber.h>
#include <rs/subscription.h>

namespace shk {
namespace detail {

template <typename InnerSubscriberType, typename Element>
class MergeSubscription : public SubscriptionBase {
  class MergeSubscriber : public SubscriberBase {
   public:
    MergeSubscriber(
        size_t idx,
        const std::shared_ptr<MergeSubscription> &merge_subscription)
        : idx_(idx),
          merge_subscription_(merge_subscription) {}

    void OnNext(Element &&elm) {
      if (auto merge_subscription = merge_subscription_.lock()) {
        merge_subscription->OnInnerSubscriptionNext(idx_, std::move(elm));
      }
    }

    void OnError(std::exception_ptr &&error) {
      if (auto merge_subscription = merge_subscription_.lock()) {
        merge_subscription->OnInnerSubscriptionError(std::move(error));
      }
    }

    void OnComplete() {
      if (auto merge_subscription = merge_subscription_.lock()) {
        merge_subscription->OnInnerSubscriptionComplete();
      }
    }

   private:
    size_t idx_;
    std::weak_ptr<MergeSubscription> merge_subscription_;
  };

  struct MergeSubscriptionData {
    template <typename SubscriptionT>
    explicit MergeSubscriptionData(SubscriptionT &&subscription)
        : subscription(Subscription(
              std::forward<SubscriptionT>(subscription))) {}

    Subscription subscription;
    ElementCount outstanding;
  };

  template <size_t Idx, typename ...Publishers>
  struct MergeSubscriptionBuilder {
    static void Subscribe(
        const std::shared_ptr<MergeSubscription> &merge_subscription,
        std::vector<MergeSubscriptionData> &subscriptions,
        const std::tuple<Publishers...> &publishers) {
      if (merge_subscription->finished_) {
        // A previous Publisher has already on Subscribe failed. We should not
        // continue.
        return;
      }
      subscriptions.emplace_back(
          std::get<Idx>(publishers).Subscribe(
              MergeSubscriber(Idx, merge_subscription)));
      MergeSubscriptionBuilder<Idx + 1, Publishers...>::Subscribe(
          merge_subscription,
          subscriptions,
          publishers);
    }
  };

  template <typename ...Publishers>
  struct MergeSubscriptionBuilder<sizeof...(Publishers), Publishers...> {
    static void Subscribe(
        const std::shared_ptr<MergeSubscription> &merge_subscription,
        std::vector<MergeSubscriptionData> &subscriptions,
        const std::tuple<Publishers...> &publishers) {
      // Terminate the template recursion
    }
  };

 public:
  MergeSubscription(InnerSubscriberType &&inner_subscriber)
      : inner_subscriber_(std::move(inner_subscriber)) {}

  template <typename ...Publishers>
  void Subscribe(
      const std::shared_ptr<MergeSubscription> &me,
      const std::tuple<Publishers...> &publishers) {
    remaining_subscriptions_ = sizeof...(Publishers);
    subscriptions_.reserve(sizeof...(Publishers));
    MergeSubscriptionBuilder<0, Publishers...>::Subscribe(
        me, subscriptions_, publishers);

    if (sizeof...(Publishers) == 0) {
      SendOnComplete();
    }
  }

  void Request(ElementCount count) {
    if (finished_) {
      return;
    }

    bool request_is_already_being_called =
        requested_elements_being_processed_ != 0;
    requested_elements_being_processed_ += count;
    if (request_is_already_being_called) {
      return;
    }

    outstanding_ += requested_elements_being_processed_;

    while (requested_elements_being_processed_ > 0 && !buffer_.empty()) {
      inner_subscriber_.OnNext(std::move(buffer_.front()));

      // Need to decrement this after calling OnNext, to ensure that re-entrant
      // Request calls always see that they are re-entrant.
      requested_elements_being_processed_--;
      buffer_.pop_front();
    }

    if (requested_elements_being_processed_ > 0) {
      for (auto &subscription_data : subscriptions_) {
        auto to_request =
            requested_elements_being_processed_ -
            subscription_data.outstanding;
        if (to_request > 0) {
          subscription_data.outstanding += to_request;
          subscription_data.subscription.Request(to_request);
        }
      }
    }

    // Need to reset requested_elements_being_processed_ in case it's Infinite
    requested_elements_being_processed_ = 0;

    MaybeSendOnComplete();
  }

  void Cancel() {
    finished_ = true;
    for (auto &subscription_data : subscriptions_) {
      subscription_data.subscription.Cancel();
    }
  }

 private:
  void OnInnerSubscriptionNext(size_t idx, Element &&element) {
    if (finished_) {
      return;
    }

    if (idx >= subscriptions_.size()) {
      // This happens if the Publisher starts emitting values during the
      // Subscribe method.
      OnInnerSubscriptionError(std::make_exception_ptr(
          std::logic_error("Got value before Requesting anything")));
      return;
    }

    auto &outstanding = subscriptions_[idx].outstanding;
    if (outstanding <= 0) {
      OnInnerSubscriptionError(std::make_exception_ptr(
          std::logic_error("Got value that was not Request-ed")));
      return;
    }
    --outstanding;

    if (outstanding_ > 0) {
      --outstanding_;
      inner_subscriber_.OnNext(std::move(element));
    } else {
      buffer_.emplace_back(std::move(element));
    }
  }

  void OnInnerSubscriptionError(std::exception_ptr &&error) {
    if (!finished_) {
      Cancel();
      inner_subscriber_.OnError(std::move(error));
    }
  }

  void OnInnerSubscriptionComplete() {
    remaining_subscriptions_--;
    MaybeSendOnComplete();
  }

  void MaybeSendOnComplete() {
    if (!finished_ && remaining_subscriptions_ == 0 && buffer_.empty()) {
      SendOnComplete();
    }
  }

  void SendOnComplete() {
    finished_ = true;
    inner_subscriber_.OnComplete();
  }

  std::deque<Element> buffer_;
  // The number of elements that have been requested from this stream that have
  // not yet been delivered.
  ElementCount outstanding_;

  // The number of elements that have been asked to be processed by calling
  // Request that it has not yet had the time to request. This is used to make
  // sure that in the cases where Request calls OnNext and OnNext calls Request,
  // it does not call OnNext again.
  ElementCount requested_elements_being_processed_;

  size_t remaining_subscriptions_ = 0;
  bool finished_ = false;
  InnerSubscriberType inner_subscriber_;
  // With each subscription, also keep track of how many requested elements are
  // outstanding. This is used to make sure that for any given subscription, we
  // don't have more elements Requested than what has been requested for the
  // merged stream. This is necessary to stay within the documented upper bound
  // for buffer size.
  //
  // This is empty until the call to Subscribe and may be empty after
  // cancellation.
  std::vector<MergeSubscriptionData> subscriptions_;
};

}  // namespace detail

/**
 * Merge combines multiple streams into one. All elements of the incoming
 * streams are emitted in the combined stream.
 *
 * In order to not violate the backpressure invariants, Merge may need to buffer
 * up to [number of input streams - 1] * [outstanding requested elements]
 * elements. If an infinite number of elements are requested, no buffering is
 * performed.
 */
template <typename Element, typename ...Publishers>
auto Merge(Publishers &&...publishers) {
  return MakePublisher([publishers = std::make_tuple(
      std::forward<Publishers>(publishers)...)](auto &&subscriber) {
    auto merge_subscription = std::make_shared<detail::MergeSubscription<
        typename std::decay<decltype(subscriber)>::type,
        Element>>(
            std::forward<decltype(subscriber)>(subscriber));

    merge_subscription->Subscribe(merge_subscription, publishers);

    return MakeSubscription(merge_subscription);
  });
}

}  // namespace shk
