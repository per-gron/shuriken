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

#include <utility>

#include <grpc++/grpc++.h>

namespace shk {
namespace detail {

class RxGrpcIdentityTransform {
 public:
  RxGrpcIdentityTransform() = delete;

  template <typename T>
  static std::pair<T, grpc::Status> wrap(T &&value) {
    return std::make_pair(std::forward<T>(value), grpc::Status::OK);
  }

  template <typename T>
  static T unwrap(T &&value) {
    return std::forward<T>(value);
  }
};

}  // namespace detail
}  // namespace shk
