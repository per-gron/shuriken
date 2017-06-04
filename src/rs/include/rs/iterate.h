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
#include <rs/subscription.h>

namespace shk {

/**
 * Iterate takes a C++ container, for example an std::vector and returns a
 * Publisher that emits the values of that container.
 *
 * Iterate makes a copy of the container every time the Publisher is subscribed
 * to, in order to be able to give ownership of each value in the container to
 * its Subscriber.
 */
template <typename Container>
auto Iterate(Container &&container) {
  return MakePublisher([container = std::forward<Container>(container)](
      auto subscriber) {
    class ContainerSubscription : public SubscriptionBase {
     public:
      ContainerSubscription(
          const typename std::decay<Container>::type &container,
          decltype(subscriber) &&subscriber)
          : container_(container),
            subscriber_(std::move(subscriber)),
            it_(std::begin(container_)),
            end_(std::end(container_)) {
      if (it_ == end_) {
        subscriber_.OnComplete();
      }
    }

    void Request(ElementCount count) {
      bool has_outstanding_request_count = outstanding_request_count_ != 0;
      outstanding_request_count_ += count;
      if (has_outstanding_request_count) {
        // Farther up in the stack, Request is already being called. No need
        // to do anything here.
        return;
      }

      while (!cancelled_ && outstanding_request_count_ != 0 && !(it_ == end_)) {
        auto &&value = *it_;
        ++it_;
        subscriber_.OnNext(std::move(value));
        if (it_ == end_) {
          outstanding_request_count_ = 0;  // Just for sanity, not needed
          subscriber_.OnComplete();
        }

        // Need to decrement this after calling OnNext/OnComplete, to ensure
        // that re-entrant Request calls always see that they are re-entrant.
        outstanding_request_count_--;
      }
    }

    void Cancel() {
      cancelled_ = true;
    }

     private:
      typename std::decay<Container>::type container_;
      decltype(subscriber) subscriber_;
      decltype(std::begin(std::declval<Container>())) it_;
      decltype(std::end(std::declval<Container>())) end_;
      bool cancelled_ = false;
      ElementCount outstanding_request_count_ = ElementCount(0);
    };

    return ContainerSubscription(
        container,
        std::move(subscriber));
  });
}

}  // namespace shk
