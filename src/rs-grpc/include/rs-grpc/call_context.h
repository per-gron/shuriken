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

class CallContextBuilder;

}  // namespace detail

template <typename>
class RsGrpcClient;

/**
 * A CallContext is an opaque object that represents a context in which an RPC
 * can be made: RPCs can be done as part of handling RPCs in a server, or they
 * can be made directly on a runloop independently from an incoming RPC.
 *
 * CallContexts are cheap to copy.
 */
class CallContext {
 public:
 private:
  friend class detail::CallContextBuilder;
  template <typename>
  friend class RsGrpcClient;

  // cq must not be null
  explicit CallContext(::grpc::CompletionQueue *cq) : cq_(cq) {}

  ::grpc::CompletionQueue *cq_;
};

namespace detail {

class CallContextBuilder {
 public:
  static CallContext Build(::grpc::CompletionQueue *cq) {
    return CallContext(cq);
  }
};

}  // namespace detail
}  // namespace shk
