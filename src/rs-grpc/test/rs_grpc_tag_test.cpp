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

#include <rs-grpc/rs_grpc_tag.h>

namespace shk {
namespace detail {
namespace {

class MockRsGrpcTag : public RsGrpcTag {
 public:
  MockRsGrpcTag(bool *destroyed) : destroyed_(*destroyed) {}

  ~MockRsGrpcTag() {
    CHECK(!destroyed_);
    destroyed_ = true;
  }

  void operator()(bool success) override {
  }

 private:
  bool &destroyed_;
};

}  // anonymous namespace

TEST_CASE("RsGrpcTag") {
  // TODO(peck): Move rs_grpc_tag.h to a detail/ directory

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
        RsGrpcTag::Ptr ptr;
        CHECK(!ptr);
        CHECK(ptr.Get() == nullptr);
      }

      SECTION("ToShared") {
        {
          auto *tag = new MockRsGrpcTag(&destroyed);
          auto ptr = tag->ToShared();
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
          const auto ptr = tag->ToShared();
          CHECK(!destroyed);
          tag->Release();
          CHECK(!destroyed);

          CHECK(ptr);
          CHECK(ptr.Get() == tag);
        }
        CHECK(destroyed);
      }

      SECTION("Reset") {
        auto *tag = new MockRsGrpcTag(&destroyed);
        auto ptr = tag->ToShared();
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
          auto ptr = tag->ToShared();
          CHECK(!destroyed);
          tag->Release();
          CHECK(!destroyed);

          RsGrpcTag::Ptr copy(ptr);
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
          auto ptr = tag->ToShared();
          CHECK(!destroyed);
          tag->Release();
          CHECK(!destroyed);

          RsGrpcTag::Ptr copy;
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
          auto ptr = tag->ToShared();
          CHECK(!destroyed);
          tag->Release();
          CHECK(!destroyed);

          RsGrpcTag::Ptr moved(std::move(ptr));
          CHECK(static_cast<void *>(tag) == static_cast<void *>(moved.Get()));
          CHECK(!ptr);
          CHECK(!destroyed);
        }
        CHECK(destroyed);
      }

      SECTION("move assignment") {
        {
          auto *tag = new MockRsGrpcTag(&destroyed);
          auto ptr = tag->ToShared();
          CHECK(!destroyed);
          tag->Release();
          CHECK(!destroyed);

          RsGrpcTag::Ptr moved;
          moved = std::move(ptr);
          CHECK(static_cast<void *>(tag) == static_cast<void *>(moved.Get()));
          CHECK(!ptr);
          CHECK(!destroyed);
        }
        CHECK(destroyed);
      }
    }
  }
}

}  // namespace detail
}  // namespace shk
