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

#include <rs/subscriber.h>
#include <rs/subscription.h>

namespace shk {
namespace detail {

template <typename Accumulator, typename Subscriber, typename Reducer>
class StreamReducer : public SubscriberBase {
 public:
  StreamReducer(
      Accumulator &&accumulator,
      Subscriber &&subscriber,
      const Reducer &reducer)
      : accumulator_(std::move(accumulator)),
        subscriber_(std::move(subscriber)),
        reducer_(reducer) {}

  template <typename T>
  void OnNext(T &&t) {
    if (failed_) {
      return;
    }

    try {
      accumulator_ = reducer_(std::move(accumulator_), std::forward<T>(t));
    } catch (...) {
      failed_ = true;
      subscriber_.OnError(std::current_exception());
    }
  }

  void OnError(std::exception_ptr &&error) {
    failed_ = true;
    subscriber_.OnError(std::move(error));
  }

  void OnComplete() {
    if (!failed_) {
      RequestedResult();
    }
  }

  void RequestedResult() {
    state_++;
    if (state_ == 2) {
      subscriber_.OnNext(std::move(accumulator_));
      subscriber_.OnComplete();
    }
  }

 private:

  int state_ = 0;

  bool failed_ = false;
  Accumulator accumulator_;
  Subscriber subscriber_;
  Reducer reducer_;
};

}  // namespace detail

/**
 * Like Reduce, but takes a function that returns the initial value instead of
 * the initial value directly. This is useful if the initial value is not
 * copyable.
 */
template <typename MakeInitial, typename Reducer>
auto ReduceGet(MakeInitial &&make_initial, Reducer &&reducer) {
  // Return an operator (it takes a Publisher and returns a Publisher)
  return [
      make_initial = std::forward<MakeInitial>(make_initial),
      reducer = std::forward<Reducer>(reducer)](auto source) {
    // Return a Publisher
    return [make_initial, reducer, source = std::move(source)](
        auto &&subscriber) {
      auto stream_reducer = std::make_shared<
          detail::StreamReducer<
              typename std::decay<decltype(make_initial())>::type,
              typename std::decay<decltype(subscriber)>::type,
              Reducer>>(
                  make_initial(),
                  std::forward<decltype(subscriber)>(subscriber),
                  reducer);
      auto sub = source(MakeSubscriber(stream_reducer));

      return MakeSubscription(
          [stream_reducer, sub = std::move(sub)](size_t count) mutable {
            if (count > 0) {
              sub.Request(Subscription::kAll);
              stream_reducer->RequestedResult();
            }
          });
    };
  };
}

/**
 * Like the reduce / fold operator in functional programming but over lists.
 *
 * Takes a stream of values and returns a stream of exactly one value.
 *
 * Initial must be copyable. If it isn't, consider using ReduceGet.
 */
template <typename Accumulator, typename Reducer>
auto Reduce(Accumulator &&initial, Reducer &&reducer) {
  return ReduceGet(
      [initial] { return initial; },
      std::forward<Reducer>(reducer));
}

}  // namespace shk
