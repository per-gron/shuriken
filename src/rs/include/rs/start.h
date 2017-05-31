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
#include <rs/subscription.h>

namespace shk {
namespace detail {

template <typename CreateValue, typename Subscriber>
class StartSubscription : public SubscriptionBase {
 public:
  template <typename SubscriberT>
  StartSubscription(const CreateValue &create_value, SubscriberT &&subscriber)
      : create_value_(create_value),
        subscriber_(std::forward<SubscriberT>(subscriber)) {}

  void Request(size_t count) {
    if (!_cancelled && count != 0) {
      _cancelled = true;
      subscriber_.OnNext(create_value_());
      subscriber_.OnComplete();
    }
  }

  void Cancel() {
    _cancelled = true;
  }

 private:
  CreateValue create_value_;
  Subscriber subscriber_;
  bool _cancelled = false;
};

}  // namespace detail

template <typename CreateValue>
auto Start(CreateValue &&create_value) {
  return MakePublisher([create_value = std::forward<CreateValue>(create_value)](
      auto &&subscriber) {
    return detail::StartSubscription<
        typename std::decay<CreateValue>::type,
        typename std::decay<decltype(subscriber)>::type>(
            create_value,
            std::forward<decltype(subscriber)>(subscriber));
  });
}

}  // namespace shk
