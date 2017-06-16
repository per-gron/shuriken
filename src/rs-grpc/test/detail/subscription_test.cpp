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

#include <catch.hpp>

#include <rs/subscription.h>
#include <rs-grpc/detail/subscription.h>
#include <rs-grpc/detail/rs_grpc_tag.h>

namespace shk {
namespace detail {
namespace {

class TestSubscription : public RsGrpcTag, public SubscriptionBase {
 public:
  void Request(ElementCount count) {
    request_invocations_++;
  }

  void Cancel() {
    cancel_invocations_++;
  }

  void TagOperationDone(bool success) override {
  }

  using RsGrpcTag::ToShared;

  int RequestInvocations() const {
    return request_invocations_;
  }

  int CancelInvocations() const {
    return cancel_invocations_;
  }

 public:
  int request_invocations_ = 0;
  int cancel_invocations_ = 0;
};

}  // anonymous namespace

TEST_CASE("Subscription") {
  auto ptr = RsGrpcTag::Ptr<TestSubscription>::TakeOver(
      new TestSubscription());

  SECTION("Move") {
    auto sub = MakeRsGrpcTagSubscription(ptr);
    auto moved_sub = std::move(sub);
  }

  SECTION("Request") {
    {
      auto sub = MakeRsGrpcTagSubscription(ptr);

      CHECK(ptr->RequestInvocations() == 0);
      sub.Request(ElementCount(13));
      CHECK(ptr->RequestInvocations() == 1);
    }
    CHECK(ptr->RequestInvocations() == 1);
    CHECK(ptr->CancelInvocations() == 0);
  }

  SECTION("Cancel") {
    {
      auto sub = MakeRsGrpcTagSubscription(ptr);

      CHECK(ptr->CancelInvocations() == 0);
      sub.Cancel();
      CHECK(ptr->CancelInvocations() == 1);
    }
    CHECK(ptr->CancelInvocations() == 1);
    CHECK(ptr->RequestInvocations() == 0);
  }
}

}  // namespace detail
}  // namespace shk
