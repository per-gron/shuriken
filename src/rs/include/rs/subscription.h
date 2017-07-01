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

#include <rs/element_count.h>

namespace shk {

/**
 * Classes that conform to the Subscription concept should inherit from this
 * class to signify that they are a Subscription.
 *
 * Subscription types must have the following methods:
 *
 * * void Request(ElementCount count);
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
  EmptySubscription() = default;

  EmptySubscription(const EmptySubscription &) = delete;
  EmptySubscription& operator=(const EmptySubscription &) = delete;

  EmptySubscription(EmptySubscription &&) = default;
  EmptySubscription& operator=(EmptySubscription &&) = default;

  void Request(ElementCount count);
  void Cancel();
};


template <typename RequestCb, typename CancelCb>
class CallbackSubscription : public SubscriptionBase {
 public:
  template <typename RequestCbT, typename CancelCbT>
  CallbackSubscription(RequestCbT &&request, CancelCbT &&cancel)
      : request_(std::forward<RequestCbT>(request)),
        cancel_(std::forward<CancelCbT>(cancel)) {}

  CallbackSubscription(const CallbackSubscription &) = delete;
  CallbackSubscription &operator=(const CallbackSubscription &) = delete;

  CallbackSubscription(CallbackSubscription &&) = default;
  CallbackSubscription &operator=(CallbackSubscription &&) = default;

  void Request(ElementCount count) {
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

  SharedPtrSubscription(const SharedPtrSubscription &) = delete;
  SharedPtrSubscription& operator=(const SharedPtrSubscription &) = delete;

  SharedPtrSubscription(SharedPtrSubscription &&) = default;
  SharedPtrSubscription& operator=(SharedPtrSubscription &&) = default;

  void Request(ElementCount count) {
    subscription_->Request(count);
  }

  void Cancel() {
    subscription_->Cancel();
  }

 private:
  std::shared_ptr<SubscriptionType> subscription_;
};

class SubscriptionEraser {
 public:
  virtual ~SubscriptionEraser();
  virtual void Request(ElementCount count) = 0;
  virtual void Cancel() = 0;
};

template <typename S>
class AnySubscriptionEraser : public detail::SubscriptionEraser {
 public:
  template <typename SType>
  AnySubscriptionEraser(SType &&subscription)
      : subscription_(std::forward<SType>(subscription)) {}

  void Request(ElementCount count) override {
    subscription_.Request(count);
  }

  void Cancel() override {
    subscription_.Cancel();
  }

 private:
  // Sanity check... it's really bad if the user of this class forgets to
  // std::decay the template parameter if necessary.
  static_assert(
      !std::is_reference<S>::value,
      "Subscription type must be held by value");
  S subscription_;
};

}  // namespace detail

template <typename T>
constexpr bool IsSubscription = std::is_base_of<SubscriptionBase, T>::value;

/**
 * Type erasure wrapper for Subscription objects that owns the erased
 * Subscription via unique_ptr.
 */
class Subscription : public SubscriptionBase {
 public:
  Subscription();

  /**
   * S should implement the Subscription concept.
   */
  template <
      typename S,
      class = typename std::enable_if<IsSubscription<
          typename std::remove_reference<S>::type>>::type>
  explicit Subscription(S &&s)
      : eraser_(std::make_unique<detail::AnySubscriptionEraser<
            typename std::decay<S>::type>>(std::forward<S>(s))) {}

  Subscription(const Subscription &) = delete;
  Subscription &operator=(const Subscription &) = delete;

  Subscription(Subscription &&) = default;
  Subscription &operator=(Subscription &&) = default;

  void Request(ElementCount count);
  void Cancel();

 private:
  std::unique_ptr<detail::SubscriptionEraser> eraser_;
};

/**
 * Type erasure wrapper for Subscription objects that owns the erased´
 * subscription via shared_ptr. Unless you need the shared_ptr aspect, it's
 * usually a good idea to use Subscription (which uses unique_ptr)
 */
class SharedSubscription : public SubscriptionBase {
 public:
  SharedSubscription();

  /**
   * S should implement the Subscription concept.
   */
  template <
      typename S,
      class = typename std::enable_if<IsSubscription<
          typename std::remove_reference<S>::type>>::type>
  explicit SharedSubscription(S &&s)
      : eraser_(std::make_shared<detail::AnySubscriptionEraser<
            typename std::decay<S>::type>>(std::forward<S>(s))) {}

  void Request(ElementCount count);
  void Cancel();

 private:
  friend class WeakSubscription;

  std::shared_ptr<detail::SubscriptionEraser> eraser_;
};

/**
 * Type erasure wrapper for Subscription objects that owns the erased´
 * subscription via shared_ptr. Unless you need the shared_ptr aspect, it's
 * usually a good idea to use Subscription (which uses unique_ptr)
 */
class WeakSubscription : public SubscriptionBase {
 public:
  WeakSubscription();
  explicit WeakSubscription(const SharedSubscription &s);

  void Request(ElementCount count);
  void Cancel();

 private:
  std::weak_ptr<detail::SubscriptionEraser> eraser_;
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
