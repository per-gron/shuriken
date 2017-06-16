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

#include <rs-grpc/detail/rs_grpc_tag.h>

namespace shk {
namespace detail {

RsGrpcTag::Refcount::Refcount() : data_(nullptr), local_data_(1L) {}

RsGrpcTag::Refcount::Refcount(const Refcount &other)
    : data_(other.data_),
      local_data_(other.local_data_) {
  if (data_) {
    data_->Retain();
  } else {
    // Copying a Refcount that did not have a heap-allocated data_ means
    // that we have to allocate.
    data_ = new Data;
    data_->Retain();
    data_->data = local_data_;
    other.data_ = data_;
  }
}

RsGrpcTag::Refcount &RsGrpcTag::Refcount::operator=(const Refcount &other) {
  this->~Refcount();
  data_ = other.data_;
  local_data_ = other.local_data_;
  if (data_) {
    data_->Retain();
  } else {
    // Copying a Refcount that did not have a heap-allocated data_ means
    // that we have to allocate.
    data_ = new Data;
    data_->Retain();
    data_->data = local_data_;
    other.data_ = data_;
  }
  return *this;
}

RsGrpcTag::Refcount::Refcount(Refcount &&other)
    : data_(other.data_),
      local_data_(other.local_data_) {
  other.data_ = nullptr;
}

RsGrpcTag::Refcount &RsGrpcTag::Refcount::operator=(Refcount &&other) {
  this->~Refcount();
  data_ = other.data_;
  local_data_ = other.local_data_;
  return *this;
}

RsGrpcTag::Refcount::~Refcount() {
  if (data_) {
    data_->Release();
  }
}

void RsGrpcTag::Refcount::Reset() {
  this->~Refcount();
  data_ = nullptr;
}

long &RsGrpcTag::Refcount::operator*() {
  return data_ ? data_->data : local_data_;
}

const long &RsGrpcTag::Refcount::operator*() const {
  return data_ ? data_->data : local_data_;
}

void RsGrpcTag::Refcount::Data::Retain() {
  internal_refcount_++;
}

void RsGrpcTag::Refcount::Data::Release() {
  if (internal_refcount_-- == 1L) {
    delete this;
  }
}

void RsGrpcTag::Invoke(void *got_tag, bool success) {
  uintptr_t tag_int = reinterpret_cast<uintptr_t>(got_tag);
  bool alternate = tag_int & 1;
  detail::RsGrpcTag *tag = reinterpret_cast<detail::RsGrpcTag *>(tag_int & ~1);
  if (alternate) {
    tag->AlternateTagOperationDone(success);
  } else {
    tag->TagOperationDone(success);
  }
  // Must release after invoking the tag because this could destroy tag
  tag->Release();
}

bool RsGrpcTag::ProcessOneEvent(grpc::CompletionQueue *cq) {
  void *got_tag;
  bool success = false;
  if (!cq->Next(&got_tag, &success)) {
    // Shutting down
    return false;
  } else {
    Invoke(got_tag, success);
    return true;
  }
}

void RsGrpcTag::ProcessAllEvents(grpc::CompletionQueue *cq) {
  while (ProcessOneEvent(cq)) {}
}

void RsGrpcTag::AlternateTagOperationDone(bool success) {
  throw std::logic_error("Unimplemented");
}

}  // namespace detail
}  // namespace shk
