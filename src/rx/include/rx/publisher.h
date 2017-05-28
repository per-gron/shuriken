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
  Publisher(PublisherType &&publisher)
      : eraser_(std::make_unique<PublisherEraser<PublisherType>>(
            std::forward<PublisherType>(publisher))) {}

  template <typename SubscriberType>
  Subscription operator()(SubscriberType &&subscriber) {
    static_assert(
        IsSubscriber<SubscriberType>,
        "Publisher was invoked with a non-subscriber parameter");
    return (*eraser_)(Subscriber<Ts...>(std::move(subscriber)));
  }

 private:
  class Eraser {
   public:
    virtual ~Eraser() = default;

    virtual Subscription operator()(Subscriber<Ts...> &&subscriber) = 0;
  };

  template <typename PublisherType>
  class PublisherEraser : public Eraser {
   public:
    PublisherEraser(PublisherType &&publisher)
        : publisher_(std::move(publisher)) {}

    Subscription operator()(Subscriber<Ts...> &&subscriber) override {
      return publisher_(std::move(subscriber));
    }

   private:
    PublisherType publisher_;
  };

  std::unique_ptr<Eraser> eraser_;
};

}  // namespace shk
