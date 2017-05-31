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

#include <limits>
#include <memory>
#include <type_traits>

namespace shk {

/**
 * Classes that conform to the Subscription concept should inherit from this
 * class to signify that they are a Subscription.
 *
 * Subscription types must have the following methods:
 *
 * * void Request(size_t count);
 * * void Cancel();
 *
 * Destroying a Subscription object implicitly cancels the subscription.
 */
class SubscriptionBase {
 protected:
  ~SubscriptionBase() = default;
};

namespace detail {

class EmptySubscription : public SubscriptionBase {
 public:
  void Request(size_t count);
  void Cancel();
};


template <typename RequestCb, typename CancelCb>
class CallbackSubscription : public SubscriptionBase {
 public:
  template <typename RequestCbT, typename CancelCbT>
  CallbackSubscription(RequestCbT &&request, CancelCbT &&cancel)
      : request_(std::forward<RequestCbT>(request)),
        cancel_(std::forward<CancelCbT>(cancel)) {}

  void Request(size_t count) {
    request_(count);
  }

  void Cancel() {
    cancel_();
  }

 private:
  RequestCb request_;
  CancelCb cancel_;
};

template <typename SubscriptionType>
class SharedPtrSubscription : public SubscriptionBase {
 public:
  explicit SharedPtrSubscription(std::shared_ptr<SubscriptionType> subscription)
      : subscription_(subscription) {}

  void Request(size_t count) {
    subscription_->Request(count);
  }

  void Cancel() {
    subscription_->Cancel();
  }

 private:
  std::shared_ptr<SubscriptionType> subscription_;
};

}  // namespace detail

template <typename T>
constexpr bool IsSubscription = std::is_base_of<SubscriptionBase, T>::value;

/**
 * Type erasure wrapper for Subscription objects.
 */
class Subscription : public SubscriptionBase {
 public:
  static constexpr size_t kAll = std::numeric_limits<size_t>::max();

  /**
   * S should implement the Subscription concept.
   */
  template <typename S>
  explicit Subscription(S &&s)
      : eraser_(std::make_unique<SubscriptionEraser<S>>(std::forward<S>(s))) {}

  Subscription(const Subscription &) = delete;
  Subscription &operator=(const Subscription &) = delete;
  Subscription(Subscription &&) = default;
  Subscription &operator=(Subscription &&) = default;

  void Request(size_t count);

  void Cancel();

 private:
  class Eraser {
   public:
    virtual ~Eraser();
    virtual void Request(size_t count) = 0;
    virtual void Cancel() = 0;
  };

  template <typename S>
  class SubscriptionEraser : public Eraser {
   public:
    SubscriptionEraser(S &&subscription)
        : subscription_(std::move(subscription)) {}

    void Request(size_t count) override {
      subscription_.Request(count);
    }

    void Cancel() override {
      subscription_.Cancel();
    }

   private:
    S subscription_;
  };

  std::unique_ptr<Eraser> eraser_;
};

detail::EmptySubscription MakeSubscription();

template <typename RequestCb, typename CancelCb>
auto MakeSubscription(RequestCb &&request, CancelCb &&cancel) {
  return detail::CallbackSubscription<
      typename std::decay<RequestCb>::type,
      typename std::decay<CancelCb>::type>(
          std::forward<RequestCb>(request),
          std::forward<CancelCb>(cancel));
}

template <typename SubscriptionType>
auto MakeSubscription(const std::shared_ptr<SubscriptionType> &subscription) {
  static_assert(
      IsSubscription<SubscriptionType>,
      "MakeSubscription must be called with a Subscription");

  return detail::SharedPtrSubscription<SubscriptionType>(subscription);
}

}  // namespace shk
