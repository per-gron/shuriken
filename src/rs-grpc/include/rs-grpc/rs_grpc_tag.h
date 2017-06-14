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

#include <grpc++/grpc++.h>

namespace shk {
namespace detail {

/**
 * A RsGrpcTag object is the type of objects that rs-grpc casts to void* and
 * gives to gRPC. No other object types are allowed on CompletionQueues that
 * rs-grpc use directly, and the void*s that are given to the CompletionQueue
 * must be created with the RsGrpcTag::ToTag() method.
 *
 * RsGrpcTag has its own intrusive refcount mechanism. Classes that inherit from
 * RsGrpcTag inherits from that too. This intrusive refcount is used instead of
 * std::shared_ptr partly to avoid a noticeable amount of compile time overhead,
 * but mostly because it needs to manually meddle with the refcount:
 *
 * When ToTag() is called, the refcount is increased. When the CompletionQueue
 * gives the object back, the refcount is decreased. This allows for nearly
 * automatic memory management of RsGrpcTag objects despite CompletionQueue's
 * decidedly non-automatic memory management API style.
 */
class RsGrpcTag {
  /**
   * A class that behaves like a std::shared_ptr<long>, but without thread
   * safety. RsGrpcTag uses it to be able to implement WeakPtr.
   *
   * Refcount heap allocates only when copied.
   */
  class Refcount {
   public:
    Refcount();

    Refcount(const Refcount &other);
    Refcount &operator=(const Refcount &other);

    Refcount(Refcount &&other);
    Refcount &operator=(Refcount &&other);

    ~Refcount();

    void Reset();

    long &operator*();
    const long &operator*() const;

   private:
    class Data {
     public:
      // The number of references to the RsGrpcTag object
      long data = 1L;

      void Retain();
      void Release();

     private:
      // The number of references to the Refcount object
      long internal_refcount_ = 1L;
    };

    // If data_ is non-null, data_->data is the refcount
    mutable Data *data_;
    // If data_ is null, then local_data_ is the refcount
    long local_data_;
  };

 public:
  /**
   * Smart pointer class to RsGrpcTag that behaves like a thread-unsafe
   * std::shared_ptr.
   */
  class Ptr {
   public:
    Ptr() : tag_(nullptr) {}

    Ptr(const Ptr &other) : tag_(other.tag_) {
      if (tag_) {
        tag_->Retain();
      }
    }

    Ptr(Ptr &&other) : tag_(other.tag_) {
      other.tag_ = nullptr;
    }

    Ptr &operator=(const Ptr &other) {
      this->~Ptr();
      tag_ = other.tag_;
      if (tag_) {
        tag_->Retain();
      }
      return *this;
    }

    Ptr &operator=(Ptr &&other) {
      this->~Ptr();
      tag_ = other.tag_;
      other.tag_ = nullptr;
      return *this;
    }

    /**
     * Take ownership of a raw RsGrpcTag pointer. This method is useful when
     * creating such an object, for exception safety (it is often unsafe to
     * manually Release() later).
     */
    static Ptr TakeOver(RsGrpcTag *tag) {
      return Ptr(tag, false);
    }

    ~Ptr() {
      if (tag_) {
        tag_->Release();
      }
    }

    void Reset() {
      this->~Ptr();
      tag_ = nullptr;
    }

    explicit operator bool() const {
      return !!tag_;
    }

    RsGrpcTag *Get() const {
      return tag_;
    }

   private:
    friend class RsGrpcTag;

    Ptr(RsGrpcTag *tag, bool retain) : tag_(tag) {
      if (tag_ && retain) {
        tag_->Retain();
      }
    }

    RsGrpcTag *tag_;
  };

  /**
   * Smart pointer class to RsGrpcTag that behaves like a thread-unsafe
   * std::weak_ptr.
   */
  class WeakPtr {
   public:
    WeakPtr() = default;
    explicit WeakPtr(const Ptr &ptr)
        : tag_(ptr.Get()),
          count_(ptr.Get()->count_) {}

    WeakPtr(const WeakPtr &) = default;
    WeakPtr &operator=(const WeakPtr &) = default;

    WeakPtr(WeakPtr &&other)
        : tag_(other.tag_), count_(std::move(other.count_)) {
      other.tag_ = nullptr;
    }
    WeakPtr &operator=(WeakPtr &&other) {
      tag_ = other.tag_;
      other.tag_ = nullptr;
      count_ = std::move(other.count_);
      return *this;
    }

    void Reset() {
      tag_ = nullptr;
      count_.Reset();
    }

    Ptr Lock() const {
      return tag_ && *count_ ?
          tag_->ToShared() :
          Ptr();
    }

   private:
    RsGrpcTag *tag_ = nullptr;
    Refcount count_;
  };

  RsGrpcTag() = default;
  virtual ~RsGrpcTag() = default;

  // It's not safe to let RsGrpcTag be copyable because of its intrusive
  // refcounting. And besides, the subclasses of this class are probably not
  // going to be copyable anyway.
  RsGrpcTag(const RsGrpcTag &) = delete;
  RsGrpcTag &operator=(const RsGrpcTag &) = delete;

  virtual void operator()(bool success) = 0;

  static void Invoke(void *got_tag, bool success);

  /**
   * Block and process one asynchronous event on the given CompletionQueue.
   *
   * Returns false if the event queue is shutting down.
   */
  static bool ProcessOneEvent(grpc::CompletionQueue *cq);

  /**
   * Block and process one asynchronous event, with timeout.
   *
   * Returns false if the event queue is shutting down.
   */
  template <typename T>
  static grpc::CompletionQueue::NextStatus ProcessOneEvent(
      grpc::CompletionQueue *cq, const T& deadline) {
    void *got_tag;
    bool success = false;
    auto next_status = cq->AsyncNext(&got_tag, &success, deadline);
    if (next_status == grpc::CompletionQueue::GOT_EVENT) {
      Invoke(got_tag, success);
    }
    return next_status;
  }

  static void ProcessAllEvents(grpc::CompletionQueue *cq);

  void *ToTag() {
    Retain();
    return this;
  }

  Ptr ToShared() {
    return Ptr(this, true);
  }

  void Retain() {
    (*count_)++;
  }

  void Release() {
    if ((*count_)-- == 1L) {
      delete this;
    }
  }

 private:
  friend class WeakPtr;

  Refcount count_;
};

}  // namespace detail
}  // namespace shk
