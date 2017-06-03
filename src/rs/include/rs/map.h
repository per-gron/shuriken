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

#include <rs/publisher.h>
#include <rs/subscriber.h>
#include <rs/subscription.h>

namespace shk {
namespace detail {

template <typename InnerSubscriberType, typename Mapper>
class MapSubscriber : public SubscriberBase, public SubscriptionBase {
 public:
  MapSubscriber(InnerSubscriberType &&inner_subscriber, const Mapper &mapper)
      : inner_subscriber_(std::move(inner_subscriber)),
        subscription_(MakeSubscription()),
        mapper_(mapper) {}

  template <typename SubscriptionT>
  void TakeSubscription(SubscriptionT &&subscription) {
    subscription_ = Subscription(std::forward<SubscriptionT>(subscription));
  }

  template <typename T, class = IsRvalue<T>>
  void OnNext(T &&t) {
    if (cancelled_) {
      return;
    }

    try {
      // We're only interested in catching the exception from mapper_ here, not
      // OnNext. But the specification requires that OnNext does not throw, and
      // here we rely on that.
      inner_subscriber_.OnNext(mapper_(std::forward<T>(t)));
    } catch (...) {
      Cancel();
      inner_subscriber_.OnError(std::current_exception());
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

  void Request(size_t count) {
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
  Mapper mapper_;
};

}  // namespace detail

/**
 * Map is like the functional map operator that operates on a Publisher.
 */
template <typename Mapper>
auto Map(Mapper &&mapper) {
  // Return an operator (it takes a Publisher and returns a Publisher)
  return [mapper = std::forward<Mapper>(mapper)](auto source) {
    // Return a Publisher
    return MakePublisher([mapper, source = std::move(source)](
        auto &&subscriber) {
      auto map_subscriber = std::make_shared<detail::MapSubscriber<
          typename std::decay<decltype(subscriber)>::type,
          typename std::decay<Mapper>::type>>(
              std::forward<decltype(subscriber)>(subscriber),
              mapper);

      map_subscriber->TakeSubscription(
          source.Subscribe(MakeSubscriber(map_subscriber)));

      return MakeSubscription(map_subscriber);
    });
  };
}

}  // namespace shk
