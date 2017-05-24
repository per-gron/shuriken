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

template <typename Stream>
class StreamTraits;

template <typename ResponseType>
class StreamTraits<grpc::ServerAsyncResponseWriter<ResponseType>> {
 public:
  static constexpr bool kRequestStreaming = false;
  static constexpr bool kResponseStreaming = false;
};

template <typename ResponseType>
class StreamTraits<grpc::ServerAsyncWriter<ResponseType>> {
 public:
  static constexpr bool kRequestStreaming = false;
  static constexpr bool kResponseStreaming = true;
};

template <typename ResponseType, typename RequestType>
class StreamTraits<grpc::ServerAsyncReader<ResponseType, RequestType>> {
 public:
  static constexpr bool kRequestStreaming = true;
  static constexpr bool kResponseStreaming = false;
};

template <typename ResponseType, typename RequestType>
class StreamTraits<
    grpc::ServerAsyncReaderWriter<ResponseType, RequestType>> {
 public:
  static constexpr bool kRequestStreaming = true;
  static constexpr bool kResponseStreaming = true;
};

template <typename ResponseType>
class StreamTraits<grpc::ClientAsyncResponseReader<ResponseType>> {
 public:
  static constexpr bool kRequestStreaming = false;
  static constexpr bool kResponseStreaming = false;
};

template <typename ResponseType>
class StreamTraits<grpc::ClientAsyncWriter<ResponseType>> {
 public:
  static constexpr bool kRequestStreaming = true;
  static constexpr bool kResponseStreaming = false;
};

template <typename ResponseType>
class StreamTraits<grpc::ClientAsyncReader<ResponseType>> {
 public:
  static constexpr bool kRequestStreaming = false;
  static constexpr bool kResponseStreaming = true;
};

template <typename ResponseType, typename RequestType>
class StreamTraits<grpc::ClientAsyncReaderWriter<ResponseType, RequestType>> {
 public:
  static constexpr bool kRequestStreaming = true;
  static constexpr bool kResponseStreaming = true;
};

}  // namespace detail
}  // namespace shk
