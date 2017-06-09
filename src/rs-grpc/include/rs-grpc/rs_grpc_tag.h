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

class RsGrpcTag {
 public:
  virtual ~RsGrpcTag() = default;

  virtual void operator()(bool success) = 0;

  static void Invoke(void *got_tag, bool success) {
    detail::RsGrpcTag *tag = reinterpret_cast<detail::RsGrpcTag *>(got_tag);
    (*tag)(success);
  }

  /**
   * Block and process one asynchronous event on the given CompletionQueue.
   *
   * Returns false if the event queue is shutting down.
   */
  static bool ProcessOneEvent(grpc::CompletionQueue *cq) {
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

  static void ProcessAllEvents(grpc::CompletionQueue *cq) {
    while (ProcessOneEvent(cq)) {}
  }
};

}  // namespace detail
}  // namespace shk
