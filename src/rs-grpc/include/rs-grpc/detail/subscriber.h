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

#include <rs/subscription.h>
#include <rs-grpc/rs_grpc_tag.h>

namespace shk {
namespace detail {

template <typename SubscriberType>
class RsGrpcTagWeakPtrSubscriber : public SubscriberBase {
 public:
  explicit RsGrpcTagWeakPtrSubscriber(
      const RsGrpcTag::WeakPtr<SubscriberType> &subscriber)
      : subscriber_(subscriber) {}

  template <typename T, class = IsRvalue<T>>
  void OnNext(T &&t) {
    if (auto sub = subscriber_.Lock()) {
      sub->OnNext(std::forward<T>(t));
    }
  }

  void OnError(std::exception_ptr &&error) {
    if (auto sub = subscriber_.Lock()) {
      sub->OnError(std::move(error));
    }
  }

  void OnComplete() {
    if (auto sub = subscriber_.Lock()) {
      sub->OnComplete();
    }
  }

 private:
  RsGrpcTag::WeakPtr<SubscriberType> subscriber_;
};

template <typename SubscriberType>
auto MakeRsGrpcTagSubscriber(
    const RsGrpcTag::WeakPtr<SubscriberType> &subscription) {
  static_assert(
      IsSubscriber<SubscriberType>,
      "MakeRsGrpcTagSubscriber must be called with a Subscriber");

  return detail::RsGrpcTagWeakPtrSubscriber<SubscriberType>(subscription);
}

}  // namespace detail
}  // namespace shk
