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
#include <type_traits>

#include <rs/subscriber.h>
#include <rs/subscription.h>

namespace shk {

/**
 * Classes that conform to the Publisher concept should inherit from this class
 * to signify that they are a Publisher.
 *
 * Publisher types must have an operator() overload that takes an object that
 * conforms to the Subscriber concept by rvalue reference and that returns an
 * object that conforms to the Subscription concept.
 */
class Publisher {
 protected:
  ~Publisher() = default;
};

template <typename T>
constexpr bool IsPublisher = std::is_base_of<Publisher, T>::value;

namespace detail {

template <typename InnerPublisher>
class CallbackPublisher : public Publisher {
 public:
  struct FunctorTag {};
  /**
   * Constructor that takes a functor/lambda.
   */
  template <
      typename InnerPublisherType,
      class = std::enable_if<!IsPublisher<InnerPublisherType>>>
  explicit CallbackPublisher(
      FunctorTag, InnerPublisherType &&inner_publisher)
      : inner_publisher_(std::forward<InnerPublisherType>(inner_publisher)) {}

  template <typename T>
  auto Subscribe(T &&t) {
    return inner_publisher_(std::forward<T>(t));
  }

  template <typename T>
  auto Subscribe(T &&t) const {
    return inner_publisher_(std::forward<T>(t));
  }

 private:
  InnerPublisher inner_publisher_;
};

}  // namespace detail

/**
 * Type erasure wrapper for Publisher objects.
 */
template <typename ...Ts>
class AnyPublisher : public Publisher {
 public:
  template <typename PublisherType>
  explicit AnyPublisher(PublisherType &&publisher)
      : eraser_(std::make_shared<
            PublisherEraser<typename std::decay<PublisherType>::type>>(
                std::forward<PublisherType>(publisher))) {}

  template <typename SubscriberType>
  AnySubscription Subscribe(SubscriberType &&subscriber) const {
    static_assert(
        IsSubscriber<SubscriberType>,
        "AnyPublisher was invoked with a non-subscriber parameter");
    return eraser_->Subscribe(AnySubscriber<Ts...>(
        std::forward<SubscriberType>(subscriber)));
  }

 private:
  class Eraser {
   public:
    virtual ~Eraser() = default;

    virtual AnySubscription Subscribe(
        AnySubscriber<Ts...> &&subscriber) const = 0;
  };

  template <typename PublisherType>
  class PublisherEraser : public Eraser {
   public:
    PublisherEraser(PublisherType &&publisher)
        : publisher_(std::move(publisher)) {}

    PublisherEraser(const PublisherType &publisher)
        : publisher_(publisher) {}

    AnySubscription Subscribe(
        AnySubscriber<Ts...> &&subscriber) const override {
      return AnySubscription(publisher_.Subscribe(std::move(subscriber)));
    }

   private:
    PublisherType publisher_;
  };

  std::shared_ptr<Eraser> eraser_;
};

/**
 * Takes a functor that takes a Subscriber and returns a Subscription and
 * returns a Publisher object.
 */
template <typename PublisherType>
auto MakePublisher(PublisherType &&publisher) {
  using CallbackPublisherT = detail::CallbackPublisher<
      typename std::decay<PublisherType>::type>;
  return CallbackPublisherT(
      typename CallbackPublisherT::FunctorTag(),
      std::forward<PublisherType>(publisher));
}

}  // namespace shk
