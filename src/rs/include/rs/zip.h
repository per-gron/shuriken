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

#include <deque>
#include <type_traits>

#include <rs/element_count.h>
#include <rs/publisher.h>
#include <rs/subscriber.h>
#include <rs/subscription.h>

namespace shk {
namespace detail {

template <size_t Idx>
struct TupleMapHelper {
  template <typename Tuple, typename Mapper>
  static void Map(Tuple &&tuple, Mapper &&mapper) {
    using DecayedTuple = typename std::decay<Tuple>::type;
    mapper(std::get<std::tuple_size<DecayedTuple>::value - Idx>(tuple));
    TupleMapHelper<Idx - 1>::Map(
        std::forward<Tuple>(tuple), std::forward<Mapper>(mapper));
  }
};

template <>
struct TupleMapHelper<0> {
  template <typename Tuple, typename Mapper>
  static void Map(Tuple &&, Mapper &&) {}
};

/**
 * Takes a tuple and a mapper function and invokes the mapper on each of the
 * elements of the tuple.
 */
template <typename Tuple, typename Mapper>
void TupleMap(Tuple &&tuple, Mapper &&mapper) {
  using DecayedTuple = typename std::decay<Tuple>::type;
  TupleMapHelper<std::tuple_size<DecayedTuple>::value>::Map(
      std::forward<Tuple>(tuple), std::forward<Mapper>(mapper));
}

template <
    size_t Idx,
    typename Tuple,
    template <typename> class Map,
    typename ...Types>
struct TupleTypeMapHelper {
  using Type =
      typename TupleTypeMapHelper<
          Idx - 1,
          Tuple,
          Map,
          Types...,
          Map<std::tuple_element_t<Idx - 1, Tuple>>>::Type;
};

template <
    typename Tuple,
    template <typename> class Map,
    typename ...Types>
struct TupleTypeMapHelper<0, Tuple, Map, Types...> {
  using Type = std::tuple<Types...>;
};

/**
 * Type-level function: Takes a tuple and a Map type function and returns a
 * tuple where each element type has been mapped with the Map function.
 */
template <typename Tuple, template <typename> class Map>
using TupleTypeMap = typename TupleTypeMapHelper<
    std::tuple_size<Tuple>::value, Tuple, Map>::Type;

template <typename T>
using WrapInDeque = std::deque<T>;

template <typename Tuple, typename InnerSubscriberType, typename ...Publishers>
class ZipSubscription : public SubscriptionBase {
  // The buffer is a tuple of deques, one for each input stream.
  using Buffer = TupleTypeMap<Tuple, WrapInDeque>;
  using FinishedSubscriptions = std::bitset<sizeof...(Publishers)>;

  template <size_t Idx>
  class ZipSubscriber : public SubscriberBase {
   public:
    ZipSubscriber(
        const std::shared_ptr<ZipSubscription> &zip_subscription)
        : zip_subscription_(zip_subscription) {}

    void OnNext(std::tuple_element_t<Idx, Tuple> &&elm) {
      if (auto zip_subscription = zip_subscription_.lock()) {
        (*zip_subscription).template OnInnerSubscriptionNext<Idx>(
            std::move(elm));
      }
    }

    void OnError(std::exception_ptr &&error) {
      if (auto zip_subscription = zip_subscription_.lock()) {
        zip_subscription->OnInnerSubscriptionError(std::move(error));
      }
    }

    void OnComplete() {
      if (auto zip_subscription = zip_subscription_.lock()) {
        zip_subscription->template OnInnerSubscriptionComplete<Idx>();
      }
    }

   private:
    std::weak_ptr<ZipSubscription> zip_subscription_;
  };

  template <typename>
  using ToSubscription = Subscription;
  using Subscriptions = TupleTypeMap<Tuple, ToSubscription>;

  template <size_t Idx, typename ...InnerPublishers>
  class ZipSubscriptionBuilder {
   public:
    template <typename ...AccumulatedSubscriptions>
    static std::unique_ptr<Subscriptions> Subscribe(
        const std::shared_ptr<ZipSubscription> &zip_subscription,
        const std::tuple<Publishers...> &publishers,
        AccumulatedSubscriptions &...accumulated_subscriptions) {
      static_assert(
          IsPublisher<std::tuple_element_t<Idx, std::tuple<Publishers...>>>,
          "Zip must be given a Publisher");

      auto new_subscription = Subscription(std::get<Idx>(publishers).Subscribe(
          ZipSubscriber<Idx>(zip_subscription)));
      return ZipSubscriptionBuilder<Idx + 1, InnerPublishers...>::Subscribe(
          zip_subscription,
          publishers,
          accumulated_subscriptions...,
          new_subscription);
    }
  };

  template <typename ...InnerPublishers>
  class ZipSubscriptionBuilder<
      sizeof...(InnerPublishers), InnerPublishers...> {
   public:
    template <typename ...AccumulatedSubscriptions>
    static std::unique_ptr<Subscriptions> Subscribe(
        const std::shared_ptr<ZipSubscription> &zip_subscription,
        const std::tuple<Publishers...> &publishers,
        AccumulatedSubscriptions &...accumulated_subscriptions) {
      return std::make_unique<Subscriptions>(
          std::move(accumulated_subscriptions)...);
    }
  };

 public:
  ZipSubscription(InnerSubscriberType &&inner_subscriber)
      : inner_subscriber_(std::move(inner_subscriber)) {}

  void Subscribe(
      const std::shared_ptr<ZipSubscription> &me,
      const std::tuple<Publishers...> &publishers) {
    values_pending_ = sizeof...(Publishers);
    subscriptions_ = ZipSubscriptionBuilder<0, Publishers...>::Subscribe(
        me, publishers);
    if (finished_) {
      subscriptions_.reset();
    }

    if (sizeof...(Publishers) == 0) {
      SendOnComplete();
    }
  }

  void Request(ElementCount count) {
    if (!finished_) {
      TupleMap(*subscriptions_, [count](auto &&subscription) {
        subscription.Request(count);
      });
    }
  }

  void Cancel() {
    finished_ = true;
    if (subscriptions_) {
      TupleMap(*subscriptions_, [](auto &&subscription) {
        subscription.Cancel();
      });
    }
  }

 private:
  template <size_t Idx>
  void OnInnerSubscriptionNext(std::tuple_element_t<Idx, Tuple> &&element) {
    if (finished_) {
      return;
    }

    auto &deque = std::get<Idx>(buffer_);
    if (deque.empty()) {
      values_pending_--;
    }
    deque.emplace_back(std::move(element));

    if (values_pending_ == 0) {
      Emit();
    }
  }

  void OnInnerSubscriptionError(std::exception_ptr &&error) {
    if (!finished_) {
      Cancel();
      inner_subscriber_.OnError(std::move(error));
    }
  }

  template <size_t Idx>
  void OnInnerSubscriptionComplete() {
    finished_subscriptions_.set(Idx);
    if (std::get<Idx>(buffer_).empty()) {
      // Only if the buffer for this stream is empty is it safe to send
      // OnComplete here. If the buffer is non-empty there is a chance that
      // elements will arrive on the other streams and then the buffer should
      // be used.
      SendOnComplete();
    }
  }

  void SendOnComplete() {
    if (!finished_) {
      finished_ = true;
      inner_subscriber_.OnComplete();
    }
  }

  template <size_t Idx, typename Dummy>
  class EmittedElementBuilder {
   public:
    template <
        typename Buffer,
        typename ...AccumulatedElements>
    static Tuple Build(
        const FinishedSubscriptions &finished_subscriptions,
        bool *should_finish,
        size_t *values_pending,
        Buffer *buffer,
        AccumulatedElements &...accumulated_elements) {
      auto &deque = std::get<Idx>(*buffer);
      auto element = std::move(deque.front());
      deque.pop_front();
      if (deque.empty()) {
        (*values_pending)++;
        if (finished_subscriptions[Idx]) {
          // We have encountered a finish stream that has an empty buffer. This
          // means that this Zip stream will never emit any more elements. It's
          // time to cancel the others.
          *should_finish = true;  // TODO(peck): Test this
        }
      }
      return EmittedElementBuilder<Idx + 1, Dummy>::Build(
          finished_subscriptions,
          should_finish,
          values_pending,
          buffer,
          accumulated_elements...,
          element);
    }
  };

  template <typename Dummy>
  class EmittedElementBuilder<sizeof...(Publishers), Dummy> {
   public:
    template <
        typename Buffer,
        typename ...AccumulatedElements>
    static Tuple Build(
        const FinishedSubscriptions &finished_subscriptions,
        bool *should_finish,
        size_t *values_pending,
        Buffer *buffer,
        AccumulatedElements &...accumulated_elements) {
      return Tuple(std::move(accumulated_elements)...);
    }
  };

  void Emit() {
    bool should_finish = false;
    inner_subscriber_.OnNext(EmittedElementBuilder<0, int>::Build(
        finished_subscriptions_, &should_finish, &values_pending_, &buffer_));
    if (should_finish) {
      SendOnComplete();
    }
  }

  std::unique_ptr<Subscriptions> subscriptions_;
  FinishedSubscriptions finished_subscriptions_{};
  size_t values_pending_ = 0;
  bool finished_ = false;
  InnerSubscriberType inner_subscriber_;
  Buffer buffer_;
};

}  // namespace detail

/**
 * Zip takes a number of input streams and returns a stream of tuples containing
 * elements from all input streams combined.
 *
 * If the input streams emit different numbers of elements, the resulting stream
 * emits as many values as the smallest input stream. The other values are
 * dropped.
 *
 * Zip does internal buffering. The buffer size can be up to
 * [number of input streams - 1] * [outstanding requested elements] elements.
 * If an infinite number of elements are requested, the buffer size is
 * unbounded.
 */
template <typename Tuple, typename ...Publishers>
auto Zip(Publishers &&...publishers) {
  static_assert(
      std::tuple_size<Tuple>::value == sizeof...(Publishers),
      "Zip tuple type must have as many elements as there are Publishers");

  return MakePublisher([
      publishers = std::make_tuple(std::forward<Publishers>(publishers)...)](
          auto &&subscriber) {
    auto zip_subscription = std::make_shared<detail::ZipSubscription<
        Tuple,
        typename std::decay<decltype(subscriber)>::type,
        typename std::decay<Publishers>::type...>>(
            std::forward<decltype(subscriber)>(subscriber));

    zip_subscription->Subscribe(zip_subscription, publishers);

    return MakeSubscription(zip_subscription);
  });
}

}  // namespace shk
