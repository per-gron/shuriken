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

#include <array>
#include <memory>
#include <type_traits>

namespace shk {

/**
 * Classes that conform to the Subscriber concept should inherit from this class
 * to signify that they are a Subscriber.
 *
 * Subscriber types must have the following methods:
 *
 * * void OnNext(T &&t); -- One such method for each type that the subscriber
 *   accepts.
 * * void OnError(std::exception_ptr &&error);
 * * void OnComplete();
 */
class SubscriberBase {
 protected:
  ~SubscriberBase() = default;
};

template <typename T>
constexpr bool IsSubscriber = std::is_base_of<SubscriberBase, T>::value;

namespace detail {

template <typename Needle, int Index, typename ...Ts>
struct GetTypeIndex;

template <typename Needle, int Index>
struct GetTypeIndex<Needle, Index> {
  static constexpr int kIndex = -1;
};

template <typename Needle, int Index, typename ...Ts>
struct GetTypeIndex<Needle, Index, Needle, Ts...> {
  static constexpr int kIndex = Index;
};

template <typename Needle, int Index, typename T, typename ...Ts>
struct GetTypeIndex<Needle, Index, T, Ts...> {
  static constexpr int kIndex = GetTypeIndex<Needle, Index + 1, Ts...>::kIndex;
};

template <typename OnNextCb, typename OnErrorCb, typename OnCompleteCb>
class ConcreteSubscriber : public SubscriberBase {
 public:
  ConcreteSubscriber(
      OnNextCb &&on_next, OnErrorCb &&on_error, OnCompleteCb &&on_complete)
      : on_next_(std::forward<OnNextCb>(on_next)),
        on_error_(std::forward<OnErrorCb>(on_error)),
        on_complete_(std::forward<OnCompleteCb>(on_complete)) {}

  template <typename T>
  void OnNext(T &&t) {
    on_next_(std::forward<T>(t));
  }

  void OnError(std::exception_ptr &&error) {
    on_error_(std::move(error));
  }

  void OnComplete() {
    on_complete_();
  }

 private:
  OnNextCb on_next_;
  OnErrorCb on_error_;
  OnCompleteCb on_complete_;
};

template <typename SubscriberType>
class SharedPtrSubscriber : public SubscriberBase {
 public:
  SharedPtrSubscriber(std::shared_ptr<SubscriberType> subscriber)
      : subscriber_(subscriber) {}

  template <typename T>
  void OnNext(T &&t) {
    subscriber_->OnNext(std::forward<T>(t));
  }

  void OnError(std::exception_ptr &&error) {
    subscriber_->OnError(std::move(error));
  }

  void OnComplete() {
    subscriber_->OnComplete();
  }

 private:
  std::shared_ptr<SubscriberType> subscriber_;
};

}  // namespace detail

/**
 * Type erasure wrapper for Subscriber objects.
 */
template <typename ...Ts>
class Subscriber : public SubscriberBase {
 public:
  template <typename S>
  explicit Subscriber(
      typename std::enable_if<IsSubscriber<S>>::type &&s)
      : eraser_(std::make_unique<SubscriberEraser<S>>(std::forward<S>(s))) {}

  Subscriber(const Subscriber &) = delete;
  Subscriber &operator=(const Subscriber &) = delete;

  template <typename T>
  void OnNext(T &&t) {
    static constexpr int kIndex = detail::GetTypeIndex<T, 0, Ts...>::kIndex;
    static_assert(kIndex != -1, "Could not find matching type");
    eraser_->OnNext(kIndex, static_cast<void *>(&t));
  }

  void OnError(std::exception_ptr &&error) {
    eraser_->OnError(std::move(error));
  }

  void OnComplete() {
    eraser_->OnComplete();
  }

 private:
  class OnNextEraser {
   public:
    virtual ~OnNextEraser() = default;

    virtual void OnNext(void *object, void *rvalue_ref) const = 0;
  };

  template <typename Object, typename Parameter>
  class MethodOnNextEraser : public OnNextEraser {
   public:
    using Method = void (Object::*)(Parameter &&);

    MethodOnNextEraser(Method method)
        : method_(method) {}

    void OnNext(void *object, void *rvalue_ref) const override {
      (reinterpret_cast<Object *>(object)->*method_)(
          reinterpret_cast<Parameter &&>(rvalue_ref));
    }

   private:
    Method method_;
  };

  class Eraser {
   public:
    virtual ~Eraser() = default;

    virtual void OnNext(int index, void *rvalue_ref) = 0;
    virtual void OnError(std::exception_ptr &&error) = 0;
    virtual void OnComplete() = 0;
  };

  // TODO(peck): Make faster version for single and dual type variant
  template <typename S>
  class SubscriberEraser : public Eraser {
   public:
    SubscriberEraser(S &&subscriber)
        : subscriber_(std::move(subscriber)) {}

    void OnNext(int index, void *rvalue_ref) override {
      (*_nexts[index])(&subscriber_, rvalue_ref);
    }

    void OnError(std::exception_ptr &&error) override {
      subscriber_.OnError(std::move(error));
    }

    void OnComplete() override {
      subscriber_.OnComplete();
    }

   public:
    S subscriber_;
    std::array<const OnNextEraser *, sizeof...(Ts)> _nexts;
  };

  std::unique_ptr<Eraser> eraser_;
};

template <typename OnNextCb, typename OnErrorCb, typename OnCompleteCb>
auto MakeSubscriber(
    OnNextCb &&on_next, OnErrorCb &&on_error, OnCompleteCb &&on_complete) {
  return detail::ConcreteSubscriber<OnNextCb, OnErrorCb, OnCompleteCb>(
      std::forward<OnNextCb>(on_next),
      std::forward<OnErrorCb>(on_error),
      std::forward<OnCompleteCb>(on_complete));
}

template <typename SubscriberType>
auto MakeSubscriber(const std::shared_ptr<SubscriberType> &subscriber) {
  static_assert(
      IsSubscriber<SubscriberType>,
      "MakeSubscriber must be called with a Subscriber");

  return detail::SharedPtrSubscriber<SubscriberType>(subscriber);
}

}  // namespace shk
