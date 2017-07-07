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

#include <rs/backreference.h>
#include <rs/element_count.h>
#include <rs/publisher.h>
#include <rs/subscriber.h>
#include <rs/subscription.h>

namespace shk {
namespace detail {

/**
 * The ConcatMap class serves as a namespace with a shared set of template
 * parameters. It's there to avoid the verbosity of having to have several
 * classes with the same template parameters referring to each other.
 */
template <
    typename InnerSubscriberType,
    typename Mapper,
    typename SourcePublisher>
class ConcatMap {
 public:
  class ConcatMapValuesSubscriber;
  class ConcatMapPublishersSubscriber;

  class ConcatMapSubscription : public SubscriptionBase {
   public:
    ConcatMapSubscription(InnerSubscriberType &&inner_subscriber)
        : inner_subscriber_(std::move(inner_subscriber)) {}

    void Request(ElementCount count) {
      requested_ += count;
      subscription_.Request(count);
    }

    void Cancel() {
      subscription_.Cancel();
      publishers_subscription_.Cancel();
    }

    void SubscribeForPublishers(
        const Mapper &mapper,
        const SourcePublisher &source,
        Backreference<ConcatMapSubscription> &&self_ref_a,
        Backreference<ConcatMapSubscription> &&self_ref_b) {
      self_ref_ = std::move(self_ref_a);
      publishers_subscription_ = Subscription(source.Subscribe(
          ConcatMapPublishersSubscriber(
              mapper,
              std::move(self_ref_b))));
      publishers_subscription_.Request(ElementCount(1));
    }

   private:
    friend class ConcatMapValuesSubscriber;
    friend class ConcatMapPublishersSubscriber;

    template <typename T>
    void OnValuesNext(T &&t) {
      if (failed_) {
        // This avoids calling OnError multiple times on backpressure
        // violations.
        return;
      }

      if (requested_ == 0) {
        OnError(std::make_exception_ptr(
            std::logic_error("Got value that was not Request-ed")));
        return;
      }
      --requested_;
      inner_subscriber_.OnNext(std::forward<T>(t));
    }

    void OnError(std::exception_ptr &&error) {
      failed_ = true;
      Cancel();
      inner_subscriber_.OnError(std::move(error));
    }

    void OnValuesComplete(Backreference<ConcatMapSubscription> &&self_ref) {
      if (failed_) {
        return;
      }

      if (publishers_complete_) {
        inner_subscriber_.OnComplete();
      } else {
        self_ref_ = std::move(self_ref);
        publishers_subscription_.Request(ElementCount(1));
      }
    }

    template <typename T>
    void OnPublishersNext(T &&publisher) {
      if (failed_) {
        return;
      }

      if (!self_ref_) {
        OnError(std::make_exception_ptr(
            std::logic_error("Got value that was not Request-ed")));
        return;
      }
      last_subscription_ = std::move(subscription_);
      subscription_ = Subscription(
          publisher.Subscribe(ConcatMapValuesSubscriber(std::move(self_ref_))));
      subscription_.Request(requested_);
    }

    void OnPublishersComplete() {
      if (failed_) {
        return;
      }

      publishers_complete_ = true;
      if (self_ref_) {
        // This happens when the Publishers subscription completes when there is
        // no current values Publisher.
        inner_subscriber_.OnComplete();
      }
    }

    bool failed_ = false;
    bool publishers_complete_ = false;
    ElementCount requested_;
    // The subscription to the current values Publisher. This is re-set whenever
    // a new values Publisher is received. But beware! There is a risk that the
    // time that this class wants to set subscription_ is when the subscription_
    // object itself is this of current stack frames. In those cases, it is not
    // safe to set (and consequently destroy the previous) subscription_.
    Subscription subscription_;
    // The subscription (if any) to the latest values Publisher. This is not
    // actually read; the only reason it's there is to avoid having to destroy
    // the current subscription_ from within OnValuesComplete, which because it
    // is called from the subscription_ will destroy a this pointer on the stack
    // under its feet.
    Subscription last_subscription_;
    // During times when there is no current Publisher that is being flattened,
    // the ConcatMapSubscription holds a Backreference to itself.
    Backreference<ConcatMapSubscription> self_ref_;
    // Set once and then never set again.
    Subscription publishers_subscription_;
    // Set once (at construction) and then never set again.
    InnerSubscriberType inner_subscriber_;
  };

  class ConcatMapPublishersSubscriber : public SubscriberBase {
   public:
    ConcatMapPublishersSubscriber(
        const Mapper &mapper,
        Backreference<ConcatMapSubscription> &&subscription)
        : mapper_(mapper),
          subscription_(std::move(subscription)) {}

    template <typename T>
    void OnNext(T &&t) {
      if (!subscription_) {
        // TODO(peck): It is wrong that we don't continue here if subscription_
        // is unset. Potential fix: Make destroying the Subscription imply
        // cancellation.
        return;
      }

      try {
        auto publisher = mapper_(std::forward<T>(t));
        static_assert(
            IsPublisher<decltype(publisher)>,
            "ConcatMap mapper function must return Publisher");

        if (/*TODO(peck):Test this*/subscription_) {
          // TODO(peck): It is wrong that we don't continue here if
          // subscription_ is unset. Potential fix: Make destroying the
          // Subscription imply cancellation.

          // It is possible that calling the mapper cancels the subscription,
          // which might make subscription_ invalid. So it's good to double
          // check.
          subscription_->OnPublishersNext(std::move(publisher));
        }
      } catch (...) {
        OnError(std::current_exception());
      }
    }

    void OnError(std::exception_ptr &&error) {
      if (subscription_) {
        subscription_->OnError(std::move(error));
      }
    }

    void OnComplete() {
      if (subscription_) {
        // TODO(peck): It is wrong that we don't continue here if subscription_
        // is unset. Potential fix: Make destroying the Subscription imply
        // cancellation.

        subscription_->OnPublishersComplete();
      }
    }

   private:
    Mapper mapper_;
    Backreference<ConcatMapSubscription> subscription_;
  };

  class ConcatMapValuesSubscriber : public SubscriberBase {
   public:
    ConcatMapValuesSubscriber(
        Backreference<ConcatMapSubscription> &&subscription)
        : subscription_(std::move(subscription)) {}

    template <typename T>
    void OnNext(T &&t) {
      if (subscription_) {
        // TODO(peck): It is wrong that we don't continue here if subscription_
        // is unset. Potential fix: Make destroying the Subscription imply
        // cancellation.

        subscription_->OnValuesNext(std::forward<T>(t));
      }
    }

    void OnError(std::exception_ptr &&error) {
      if (subscription_) {
        // TODO(peck): It is wrong that we don't continue here if subscription_
        // is unset. Potential fix: Make destroying the Subscription imply
        // cancellation.

        subscription_->OnError(std::move(error));
      }
    }

    void OnComplete() {
      if (subscription_) {
        // TODO(peck): It is wrong that we don't continue here if subscription_
        // is unset. Potential fix: Make destroying the Subscription imply
        // cancellation.

        subscription_->OnValuesComplete(std::move(subscription_));
      }
    }

   private:
    friend class ConcatMapSubscription;

    Backreference<ConcatMapSubscription> subscription_;
  };
};

}  // namespace detail

/**
 * ConcatMap is like the functional flatMap operator that operates on a
 * Publisher: The mapper function returns a Publisher, which may emit zero or
 * more values. All of the Publishers returned by the mapper are concatenated,
 * or "flattened", into a single Publisher.
 */
template <typename MapperT>
auto ConcatMap(MapperT &&mapper) {
  // Return an operator (it takes a Publisher and returns a Publisher)
  return [mapper = std::forward<MapperT>(mapper)](auto source) {
    // Return a Publisher
    return MakePublisher([mapper, source = std::move(source)](
        auto &&subscriber) {
      using InnerSubscriberType =
          typename std::decay<decltype(subscriber)>::type;
      using Mapper = typename std::decay<MapperT>::type;

      using ConcatMapT = detail::ConcatMap<
          InnerSubscriberType, Mapper, decltype(source)>;

      using ConcatMapSubscriptionT =
          typename ConcatMapT::ConcatMapSubscription;

      Backreference<ConcatMapSubscriptionT> sub_ref_a;
      Backreference<ConcatMapSubscriptionT> sub_ref_b;
      auto sub = WithBackreference(
          ConcatMapSubscriptionT(
              std::forward<decltype(subscriber)>(subscriber)),
          &sub_ref_b,
          &sub_ref_a);

      sub.SubscribeForPublishers(
          mapper,
          source,
          std::move(sub_ref_a),
          std::move(sub_ref_b));

      return sub;
    });
  };
}

}  // namespace shk
