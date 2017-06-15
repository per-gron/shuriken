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

#include <rs/subscriber.h>
#include <rs-grpc/rs_grpc_tag.h>
#include <rs-grpc/subscriber.h>

namespace shk {
namespace detail {
namespace {

class TestSubscriber : public RsGrpcTag, public SubscriberBase {
 public:
  template <typename T>
  void OnNext(T &&t) {
    on_next_invocations_++;
  }

  void OnError(std::exception_ptr &&error) {
    on_error_invocations_++;
  }

  void OnComplete() {
    on_complete_invocations_++;
  }

  void operator()(bool success) override {
  }

  int OnNextInvocations() const {
    return on_next_invocations_;
  }

  int OnErrorInvocations() const {
    return on_error_invocations_;
  }

  int OnCompleteInvocations() const {
    return on_complete_invocations_;
  }

  using RsGrpcTag::ToShared;
  using RsGrpcTag::ToWeak;

 public:
  int on_next_invocations_ = 0;
  int on_error_invocations_ = 0;
  int on_complete_invocations_ = 0;
};

}  // anonymous namespace

TEST_CASE("Subscriber") {
  auto ptr = RsGrpcTag::Ptr<TestSubscriber>::TakeOver(
      new TestSubscriber());

  SECTION("type traits") {
    auto sub = MakeRsGrpcTagSubscriber(
        TestSubscriber::ToWeak<TestSubscriber>(ptr.Get()));
    static_assert(IsSubscriber<decltype(sub)>, "Should be Subscriber");
  }

  SECTION("move") {
    auto sub = MakeRsGrpcTagSubscriber(
        TestSubscriber::ToWeak<TestSubscriber>(ptr.Get()));
    auto moved_sub = std::move(sub);
  }

  SECTION("OnNext") {
    int invocations = 0;
    {
      auto sub = MakeRsGrpcTagSubscriber(
          TestSubscriber::ToWeak<TestSubscriber>(ptr.Get()));
      CHECK(ptr->OnNextInvocations() == 0);
      sub.OnNext(1337);
      CHECK(ptr->OnNextInvocations() == 1);
    }
    CHECK(ptr->OnNextInvocations() == 1);
  }

  SECTION("OnError") {
    {
      auto sub = MakeRsGrpcTagSubscriber(
          TestSubscriber::ToWeak<TestSubscriber>(ptr.Get()));
      CHECK(ptr->OnErrorInvocations() == 0);
      sub.OnError(std::make_exception_ptr(std::runtime_error("test_error")));
      CHECK(ptr->OnErrorInvocations() == 1);
    }
    CHECK(ptr->OnErrorInvocations() == 1);
  }

  SECTION("OnComplete") {
    {
      auto sub = MakeRsGrpcTagSubscriber(
          TestSubscriber::ToWeak<TestSubscriber>(ptr.Get()));
      CHECK(ptr->OnCompleteInvocations() == 0);
      sub.OnComplete();
      CHECK(ptr->OnCompleteInvocations() == 1);
    }
    CHECK(ptr->OnCompleteInvocations() == 1);
  }
}

}  // namespace detail
}  // namespace shk
