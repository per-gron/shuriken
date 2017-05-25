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
#include <rxcpp/rx.hpp>

#include "grpc_error.h"
#include "rx_grpc_tag.h"
#include "stream_traits.h"

namespace shk {
namespace detail {

/**
 * Shared logic between client and server code to write messages to a gRPC call.
 */
template <
    typename OwnerType,
    typename TransformedResponse,
    typename Transform,
    typename Stream,
    bool StreamingResponse>
class RxGrpcWriter;

/**
 * Non-streaming version.
 */
template <
    typename OwnerType,
    typename TransformedResponse,
    typename Transform,
    typename Stream>
class RxGrpcWriter<
    OwnerType, TransformedResponse, Transform, Stream, false> :
        public RxGrpcTag {
  using Context = typename StreamTraits<Stream>::Context;

 public:
  RxGrpcWriter(
      OwnerType *owner,
      Context *context)
      : _owner(*owner),
        _stream(context) {}

  Stream *get() {
    return &_stream;
  }

  template <typename Observable>
  void subscribe(Observable &&observable) {
    observable.subscribe(
        [this](TransformedResponse response) {
          _response = std::move(response);
        },
        [this](const std::exception_ptr &error) {
          _stream.FinishWithError(exceptionToStatus(error), this);
        },
        [this]() {
          _stream.Finish(Transform::unwrap(_response), grpc::Status::OK, this);
        });
  }

  void operator()(bool success) override {
    // success == false when the runloop is shutting down. No matter what the
    // value of success is, we are done.
    delete &_owner;
  }

 private:
  OwnerType &_owner;
  TransformedResponse _response;
  Stream _stream;
};

/**
 * Streaming version.
 */
template <
    typename OwnerType,
    typename TransformedResponse,
    typename Transform,
    typename Stream>
class RxGrpcWriter<
    OwnerType, TransformedResponse, Transform, Stream, true> :
        public RxGrpcTag {
  using Context = typename StreamTraits<Stream>::Context;

 public:
  RxGrpcWriter(
      OwnerType *owner,
      Context *context)
      : _owner(*owner),
        _stream(context) {}

  Stream *get() {
    return &_stream;
  }

  template <typename Observable>
  void subscribe(Observable &&observable) {
    observable.subscribe(
        [this](TransformedResponse response) {
          _enqueued_responses.emplace_back(std::move(response));
          runEnqueuedOperation();
        },
        [this](const std::exception_ptr &error) {
          _enqueued_finish_status = exceptionToStatus(error);
          _enqueued_finish = true;
          runEnqueuedOperation();
        },
        [this]() {
          _enqueued_finish_status = grpc::Status::OK;
          _enqueued_finish = true;
          runEnqueuedOperation();
        });
  }

  void operator()(bool success) override {
    if (!success) {
      // This happens when the server is shutting down.
      delete &_owner;
      return;
    }

    if (!_sent_final_response) {
      _operation_in_progress = false;
      runEnqueuedOperation();
    } else {
      delete &_owner;
    }
  }

 private:
  void runEnqueuedOperation() {
    if (_operation_in_progress) {
      return;
    }
    if (!_enqueued_responses.empty()) {
      _operation_in_progress = true;
      _stream.Write(
          Transform::unwrap(std::move(_enqueued_responses.front())), this);
      _enqueued_responses.pop_front();
    } else if (_enqueued_finish) {
      _enqueued_finish = false;
      _operation_in_progress = true;

      // Must be done before the call to Finish because it's not safe to do
      // anything after that call; gRPC could invoke the callback immediately
      // on another thread, which could delete this.
      _sent_final_response = true;

      _stream.Finish(_enqueued_finish_status, this);
    }
  }

  OwnerType &_owner;
  bool _sent_final_response = false;
  bool _operation_in_progress = false;

  // Because we don't have backpressure we need an unbounded buffer here :-(
  std::deque<TransformedResponse> _enqueued_responses;
  bool _enqueued_finish = false;
  grpc::Status _enqueued_finish_status;

  Stream _stream;
};

}  // namespace detail
}  // namespace shk
