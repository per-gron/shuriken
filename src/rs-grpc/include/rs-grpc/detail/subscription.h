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

template <typename SubscriptionType>
class RsGrpcTagPtrSubscription : public SubscriptionBase {
 public:
  explicit RsGrpcTagPtrSubscription(
      const RsGrpcTag::Ptr<SubscriptionType> &subscription)
      : subscription_(subscription) {}

  RsGrpcTagPtrSubscription(const RsGrpcTagPtrSubscription &) = delete;
  RsGrpcTagPtrSubscription& operator=(
      const RsGrpcTagPtrSubscription &) = delete;

  RsGrpcTagPtrSubscription(RsGrpcTagPtrSubscription &&) = default;
  RsGrpcTagPtrSubscription& operator=(RsGrpcTagPtrSubscription &&) = default;

  void Request(ElementCount count) {
    subscription_->Request(count);
  }

  void Cancel() {
    subscription_->Cancel();
  }

 private:
  RsGrpcTag::Ptr<SubscriptionType> subscription_;
};

template <typename SubscriptionType>
auto MakeRsGrpcTagSubscription(
    const RsGrpcTag::Ptr<SubscriptionType> &subscription) {
  static_assert(
      IsSubscription<SubscriptionType>,
      "MakeRsGrpcTagSubscription must be called with a Subscription");

  return detail::RsGrpcTagPtrSubscription<SubscriptionType>(subscription);
}

}  // namespace detail
}  // namespace shk
