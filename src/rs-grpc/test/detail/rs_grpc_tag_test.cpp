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

#include <atomic>
#include <chrono>
#include <thread>

#include <rs-grpc/detail/rs_grpc_tag.h>

namespace shk {
namespace detail {
namespace {

class MockRsGrpcTag : public RsGrpcTag {
 public:
  explicit MockRsGrpcTag(bool *destroyed)
      : alive_(true), destroyed_(*destroyed) {}

  ~MockRsGrpcTag() {
    CHECK(!destroyed_);
    destroyed_ = true;
    alive_ = false;
  }

  bool Alive() const {
    return alive_;
  }

  void TagOperationDone(bool success) override {
  }

  using RsGrpcTag::Release;
  using RsGrpcTag::Retain;
  using RsGrpcTag::ToShared;
  using RsGrpcTag::ToWeak;
  using RsGrpcTag::ToTag;

 private:
  bool alive_;
  bool &destroyed_;
};

}  // anonymous namespace

TEST_CASE("RsGrpcTag") {
  // TODO(peck): Fix asan issues

  SECTION("refcount") {
    bool destroyed = false;

    SECTION("destroy on Release") {
      auto *tag = new MockRsGrpcTag(&destroyed);
      CHECK(!destroyed);
      tag->Release();
      CHECK(destroyed);
    }

    SECTION("destroy on second Release") {
      auto *tag = new MockRsGrpcTag(&destroyed);
      tag->Retain();
      CHECK(!destroyed);
      tag->Release();
      CHECK(!destroyed);
      tag->Release();
      CHECK(destroyed);
    }

    SECTION("ToTag retains") {
      auto *tag = new MockRsGrpcTag(&destroyed);
      tag->ToTag();
      CHECK(!destroyed);
      tag->Release();
      CHECK(!destroyed);
      tag->Release();
      CHECK(destroyed);
    }

    SECTION("RsGrpcTag::Ptr") {
      SECTION("default constructor") {
        RsGrpcTag::Ptr<MockRsGrpcTag> ptr;
        CHECK(!ptr);
        CHECK(ptr.Get() == nullptr);
      }

      SECTION("ToShared") {
        {
          auto *tag = new MockRsGrpcTag(&destroyed);
          auto ptr = MockRsGrpcTag::ToShared(tag);
          CHECK(!destroyed);
          tag->Release();
          CHECK(!destroyed);

          CHECK(ptr);
          CHECK(ptr.Get() == tag);
        }
        CHECK(destroyed);
      }

      SECTION("const") {
        {
          auto *tag = new MockRsGrpcTag(&destroyed);
          const auto ptr = MockRsGrpcTag::ToShared(tag);
          CHECK(!destroyed);
          tag->Release();
          CHECK(!destroyed);

          CHECK(ptr);
          CHECK(!!ptr);
          CHECK(ptr.Get() == tag);
        }
        CHECK(destroyed);
      }

      SECTION("smart pointer operators") {
        {
          auto *tag = new MockRsGrpcTag(&destroyed);
          const auto ptr = MockRsGrpcTag::ToShared(tag);
          tag->Release();
          CHECK(ptr->Alive());
          CHECK((*ptr).Alive());
          CHECK(!destroyed);
        }
        CHECK(destroyed);
      }

      SECTION("Reset") {
        auto *tag = new MockRsGrpcTag(&destroyed);
        auto ptr = MockRsGrpcTag::ToShared(tag);
        CHECK(!destroyed);
        tag->Release();
        CHECK(!destroyed);

        ptr.Reset();
        CHECK(destroyed);
        CHECK(!ptr);
        CHECK(ptr.Get() == nullptr);
      }

      SECTION("copy") {
        {
          auto *tag = new MockRsGrpcTag(&destroyed);
          auto ptr = MockRsGrpcTag::ToShared(tag);
          CHECK(!destroyed);
          tag->Release();
          CHECK(!destroyed);

          RsGrpcTag::Ptr<MockRsGrpcTag> copy(ptr);
          CHECK(static_cast<void *>(tag) == static_cast<void *>(copy.Get()));
          CHECK(ptr);
          CHECK(!destroyed);
          ptr.Reset();
          CHECK(!destroyed);
        }
        CHECK(destroyed);
      }

      SECTION("assignment operator") {
        {
          auto *tag = new MockRsGrpcTag(&destroyed);
          auto ptr = MockRsGrpcTag::ToShared(tag);
          CHECK(!destroyed);
          tag->Release();
          CHECK(!destroyed);

          RsGrpcTag::Ptr<MockRsGrpcTag> copy;
          copy = ptr;
          CHECK(static_cast<void *>(tag) == static_cast<void *>(copy.Get()));
          CHECK(ptr);
          CHECK(!destroyed);
          ptr.Reset();
          CHECK(!destroyed);
        }
        CHECK(destroyed);
      }

      SECTION("move constructor") {
        {
          auto *tag = new MockRsGrpcTag(&destroyed);
          auto ptr = MockRsGrpcTag::ToShared(tag);
          CHECK(!destroyed);
          tag->Release();
          CHECK(!destroyed);

          RsGrpcTag::Ptr<MockRsGrpcTag> moved(std::move(ptr));
          CHECK(static_cast<void *>(tag) == static_cast<void *>(moved.Get()));
          CHECK(!ptr);
          CHECK(!destroyed);
        }
        CHECK(destroyed);
      }

      SECTION("move assignment") {
        {
          auto *tag = new MockRsGrpcTag(&destroyed);
          auto ptr = MockRsGrpcTag::ToShared(tag);
          CHECK(!destroyed);
          tag->Release();
          CHECK(!destroyed);

          RsGrpcTag::Ptr<MockRsGrpcTag> moved;
          moved = std::move(ptr);
          CHECK(static_cast<void *>(tag) == static_cast<void *>(moved.Get()));
          CHECK(!ptr);
          CHECK(!destroyed);
        }
        CHECK(destroyed);
      }

      SECTION("TakeOver") {
        {
          auto *tag = new MockRsGrpcTag(&destroyed);
          auto ptr = RsGrpcTag::Ptr<MockRsGrpcTag>::TakeOver(tag);
          CHECK(!destroyed);
        }
        CHECK(destroyed);
      }
    }

    SECTION("RsGrpcTag::WeakPtr") {
      SECTION("default constructor") {
        RsGrpcTag::WeakPtr<MockRsGrpcTag> ptr;
        CHECK(!ptr.Lock());
      }

      SECTION("from Ptr") {
        {
          auto *tag = new MockRsGrpcTag(&destroyed);
          auto ptr = MockRsGrpcTag::ToShared(tag);
          tag->Release();
          const auto weak_ptr = MockRsGrpcTag::ToWeak(ptr.Get());
          CHECK(!destroyed);
          CHECK(weak_ptr.Lock().Get() == ptr.Get());
          ptr.Reset();
          CHECK(!weak_ptr.Lock());
          CHECK(destroyed);
        }
        CHECK(destroyed);
      }

      SECTION("Reset") {
        {
          auto *tag = new MockRsGrpcTag(&destroyed);
          auto ptr = MockRsGrpcTag::ToShared(tag);
          tag->Release();
          auto weak_ptr = MockRsGrpcTag::ToWeak(ptr.Get());
          weak_ptr.Reset();
          CHECK(!weak_ptr.Lock());
        }
      }

      SECTION("copy") {
        {
          auto *tag = new MockRsGrpcTag(&destroyed);
          auto ptr = MockRsGrpcTag::ToShared(tag);
          tag->Release();
          const auto weak_ptr = MockRsGrpcTag::ToWeak(ptr.Get());
          CHECK(!destroyed);
          CHECK(weak_ptr.Lock());

          RsGrpcTag::WeakPtr<MockRsGrpcTag> copy(weak_ptr);
          CHECK(weak_ptr.Lock().Get() == copy.Lock().Get());
          ptr.Reset();
          CHECK(!copy.Lock());
          CHECK(!weak_ptr.Lock());
          CHECK(destroyed);
        }
        CHECK(destroyed);
      }

      SECTION("assignment operator") {
        {
          auto *tag = new MockRsGrpcTag(&destroyed);
          auto ptr = MockRsGrpcTag::ToShared(tag);
          tag->Release();
          const auto weak_ptr = MockRsGrpcTag::ToWeak(ptr.Get());
          CHECK(!destroyed);
          CHECK(weak_ptr.Lock());

          RsGrpcTag::WeakPtr<MockRsGrpcTag> copy;
          copy = weak_ptr;
          CHECK(weak_ptr.Lock().Get() == copy.Lock().Get());
          ptr.Reset();
          CHECK(!copy.Lock());
          CHECK(!weak_ptr.Lock());
          CHECK(destroyed);
        }
        CHECK(destroyed);
      }

      SECTION("move constructor") {
        {
          auto *tag = new MockRsGrpcTag(&destroyed);
          auto ptr = MockRsGrpcTag::ToShared(tag);
          tag->Release();
          auto weak_ptr = MockRsGrpcTag::ToWeak(ptr.Get());
          CHECK(!destroyed);
          CHECK(weak_ptr.Lock());

          RsGrpcTag::WeakPtr<MockRsGrpcTag> moved(std::move(weak_ptr));
          CHECK(!weak_ptr.Lock());
          CHECK(moved.Lock().Get() == ptr.Get());
          ptr.Reset();
          CHECK(!moved.Lock());
          CHECK(!weak_ptr.Lock());
          CHECK(destroyed);
        }
        CHECK(destroyed);
      }

      SECTION("move assignment") {
        {
          auto *tag = new MockRsGrpcTag(&destroyed);
          auto ptr = MockRsGrpcTag::ToShared(tag);
          tag->Release();
          auto weak_ptr = MockRsGrpcTag::ToWeak(ptr.Get());
          CHECK(!destroyed);
          CHECK(weak_ptr.Lock());

          RsGrpcTag::WeakPtr<MockRsGrpcTag> moved;
          moved = std::move(weak_ptr);
          CHECK(!weak_ptr.Lock());
          CHECK(moved.Lock().Get() == ptr.Get());
          ptr.Reset();
          CHECK(!moved.Lock());
          CHECK(!weak_ptr.Lock());
          CHECK(destroyed);
        }
        CHECK(destroyed);
      }
    }
  }
}

}  // namespace detail
}  // namespace shk
