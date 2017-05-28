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

#include <rx/subscriber.h>
#include <rx/subscription.h>

namespace shk {

/**
 * Classes that conform to the Publisher concept should inherit from this class
 * to signify that they are a Publisher.
 *
 * Publisher types must have an operator() overload that takes an object that
 * conforms to the Subscriber concept by rvalue reference and that returns an
 * object that conforms to the Subscription concept.
 */
class PublisherBase {
 protected:
  ~PublisherBase() = default;
};

template <typename T>
constexpr bool IsPublisher = std::is_base_of<PublisherBase, T>::value;

/**
 * Type erasure wrapper for Publisher objects.
 */
template <typename ...Ts>
class Publisher : public PublisherBase {
 public:
  template <typename PublisherType>
  Publisher(PublisherType &&publisher);

  /**
   * Callback should be a functor that takes a Subscriber by rvalue reference
   * and returns a Subscription.
   */
  template <typename Callback>
  Subscription operator()(const Callback &callback);
};

auto Empty() {
  return [](auto subscriber) {
    subscriber.OnComplete();
    return MakeSubscription();
  };
}

template <typename T>
auto Just(T &&t) {
  return [t = std::forward<T>(t)](auto subscriber) {
    return MakeSubscription(
        [
            t,
            subscriber = std::move(subscriber),
            sent = false](size_t count) mutable {
          if (!sent && count != 0) {
            sent = true;
            subscriber.OnNext(std::move(t));
            subscriber.OnComplete();
          }
        });
  };
}

#if 0
template <typename Source>
auto Count(const Source &source) {
  return [source](auto subscriber) {
    return MakeSubscription(
        [
            source,
            subscriber = std::move(subscriber),
            sent = false](size_t count) mutable {
          if (!sent && count != 0) {
            sent = true;

            auto subscription = source(
                MakeSubscriber(
                    [](auto &&next) { count++; },
                    [](std::exception_ptr &&error) { fail },
                    []() { complete }));
            subscription.Request(Subscription::kAll);
          }
        });
  };
}
#endif

}  // namespace shk
