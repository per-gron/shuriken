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
class FlatMapSubscriber : public SubscriberBase, public SubscriptionBase {
  class FlatMapValuesSubscriber : public SubscriberBase {
   public:
    FlatMapValuesSubscriber(const std::weak_ptr<FlatMapSubscriber> &that)
        : that_(that) {}

    /**
     * This is where the operator receives new flattened values.
     */
    template <typename T, class = IsRvalue<T>>
    void OnNext(T &&t) {
      if (auto that = that_.lock()) {
        that->OnNextValue(std::forward<T>(t));
      }
    }

    /**
     * Called on failures on the stream of flattened values.
     */
    void OnError(std::exception_ptr &&error) {
      if (auto that = that_.lock()) {
        that->OnError(std::move(error));
      }
    }

    /**
     * Called on complete events for the stream of flattened values.
     */
    void OnComplete() {
      if (auto that = that_.lock()) {
        that->RequestNewPublisher();
      }
    }

   private:
    std::weak_ptr<FlatMapSubscriber> that_;
  };

 public:
  FlatMapSubscriber(
      InnerSubscriberType &&inner_subscriber, const Mapper &mapper)
      : inner_subscriber_(std::move(inner_subscriber)),
        publishers_subscription_(MakeSubscription()),
        values_subscription_(MakeSubscription()),
        mapper_(mapper) {}

  template <typename Publisher>
  void Subscribe(
      std::shared_ptr<FlatMapSubscriber> me,
      Publisher &&publisher) {
    me_ = me;
    publishers_subscription_ = Subscription(
        publisher.Subscribe(MakeSubscriber(me)));
  }

  /**
   * This is where the operator receives new Publishers to be flattened.
   */
  template <typename T, class = IsRvalue<T>>
  void OnNext(T &&t) {
    if (state_ == State::END) {
      // Allow stray publishers to arrive asynchronously after cancel.
      return;
    }

    if (state_ != State::REQUESTED_PUBLISHER) {
      OnError(std::make_exception_ptr(
          std::logic_error("Got value that was not Request-ed")));
      return;
    }

    try {
      auto publisher = mapper_(std::forward<T>(t));
      static_assert(
          IsPublisher<decltype(publisher)>,
          "FlatMap mapper function must return Publisher");

      state_ = State::HAS_PUBLISHER;
      // We're only interested in catching the exception from mapper_ here, not
      // OnNext. But the specification requires that Subscribe and Request do
      // not throw, and here we rely on that.
      values_subscription_ = Subscription(
          publisher.Subscribe(FlatMapValuesSubscriber(me_.lock())));
      values_subscription_.Request(requested_);
    } catch (...) {
      OnError(std::current_exception());
    }
  }

  /**
   * Called on failures on the stream of Publishers to be flattened.
   */
  void OnError(std::exception_ptr &&error) {
    if (state_ != State::END) {
      // This cancel is needed because OnError may be called by the flattened
      // values OnError as well, and in that case cancellation is needed.
      Cancel();
      inner_subscriber_.OnError(std::move(error));
    }
  }

  /**
   * Called on complete events for the stream of Publishers to be flattened.
   */
  void OnComplete() {
    switch (state_) {
      case State::END: {
        // Already cancelled. Nothing to do
        break;
      }
      case State::INIT:  // There were no elements in the stream
      case State::REQUESTED_PUBLISHER: /* No active stream */ {
        // Needed only for sanity and to prevent sending multiple OnComplete
        // signals if the upstream sends multiple OnComplete signals.
        state_ = State::END;

        inner_subscriber_.OnComplete();
        break;
      }
      case State::HAS_PUBLISHER: {
        // There will be no more Publishers, but since there is an active one
        // we can't just finish the stream, we need to wait it out.
        state_ = State::ON_LAST_PUBLISHER;
        break;
      }
      case State::ON_LAST_PUBLISHER: {
        OnError(std::make_exception_ptr(
            std::logic_error("Got more than one OnComplete signal")));
        break;
      }
    }
  }

  void Request(size_t count) {
    requested_ += count;

    switch (state_) {
      case State::END: {
        // Already finished or cancelled. Nothing to do.
        break;
      }
      case State::REQUESTED_PUBLISHER: {
        // Waiting for the next publisher. Nothing to do.
        break;
      }
      case State::HAS_PUBLISHER:
      case State::ON_LAST_PUBLISHER: {
        values_subscription_.Request(count);
        break;
      }
      case State::INIT: {
        RequestNewPublisher();
        break;
      }
    }
  }

  void Cancel() {
    publishers_subscription_.Cancel();
    values_subscription_.Cancel();
    state_ = State::END;
  }

 private:
  enum class State {
    INIT,
    REQUESTED_PUBLISHER,
    HAS_PUBLISHER,
    ON_LAST_PUBLISHER,
    END
  };

  template <typename T>
  void OnNextValue(T &&t) {
    if (requested_ == 0) {
      OnError(std::make_exception_ptr(
          std::logic_error("Got value that was not Request-ed")));
      return;
    }

    requested_--;
    inner_subscriber_.OnNext(std::forward<T>(t));
  }

  void RequestNewPublisher() {
    if (state_ == State::ON_LAST_PUBLISHER) {
      state_ = State::END;
      inner_subscriber_.OnComplete();
    } else if (requested_) {
      state_ = State::REQUESTED_PUBLISHER;
      publishers_subscription_.Request(1);
    } else if (state_ != State::END) {
      // There are no requested elements. Go back to the INIT state and wait
      // for more Requests.
      state_ = State::INIT;
    }
  }

  std::weak_ptr<FlatMapSubscriber> me_;
  size_t requested_ = 0;
  InnerSubscriberType inner_subscriber_;
  State state_ = State::INIT;
  Subscription publishers_subscription_;
  Subscription values_subscription_;
  Mapper mapper_;
};

}  // namespace detail

/**
 * Map is like the functional flatMap operator that operates on a Publisher: The
 * mapper function returns a Publisher, which may emit zero or more values. All
 * of the Publishers returned by the mapper are concatenated, or "flattened",
 * into a single Publisher.
 */
template <typename Mapper>
auto FlatMap(Mapper &&mapper) {
  // Return an operator (it takes a Publisher and returns a Publisher)
  return [mapper = std::forward<Mapper>(mapper)](auto source) {
    // Return a Publisher
    return MakePublisher([mapper, source = std::move(source)](
        auto &&subscriber) {
      auto flat_map_subscriber = std::make_shared<detail::FlatMapSubscriber<
          typename std::decay<decltype(subscriber)>::type,
          typename std::decay<Mapper>::type>>(
              std::forward<decltype(subscriber)>(subscriber),
              mapper);

      flat_map_subscriber->Subscribe(flat_map_subscriber, source);

      return MakeSubscription(flat_map_subscriber);
    });
  };
}

}  // namespace shk
