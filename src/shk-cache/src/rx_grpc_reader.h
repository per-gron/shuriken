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

#include <type_traits>

#include <grpc++/grpc++.h>
#include <rxcpp/rx.hpp>

#include "grpc_error.h"
#include "rx_grpc_tag.h"
#include "stream_traits.h"

namespace shk {
namespace detail {

/**
 * Shared logic between client and server code to read messages from a gRPC
 * call.
 */
template <
    typename OwnerType,
    typename MessageType,
    typename Transform,
    typename Stream,
    bool Streaming>
class RxGrpcReader;

/**
 * Non-streaming version
 */
template <
    typename OwnerType,
    typename MessageType,
    typename Transform,
    typename Stream>
class RxGrpcReader<
    OwnerType, MessageType, Transform, Stream, false> : public RxGrpcTag {
  using Context = typename StreamTraits<Stream>::Context;
  using TransformedMessageType = typename decltype(
      Transform::wrap(std::declval<MessageType>()))::first_type;

 public:
  RxGrpcReader(
      rxcpp::subscriber<TransformedMessageType> &&subscriber,
      Context * /*context*/,
      OwnerType *to_delete)
      : _subscriber(std::move(subscriber)),
        _to_delete(*to_delete) {}

  void operator()(bool success) override {
    if (!success) {
      // Unfortunately, gRPC provides literally no information other than that
      // the operation failed.
      _subscriber.on_error(std::make_exception_ptr(GrpcError(grpc::Status(
          grpc::UNKNOWN, "The async function encountered an error"))));
    } else if (_status.ok()) {
      auto wrapped = Transform::wrap(std::move(_response));
      if (wrapped.second.ok()) {
        _subscriber.on_next(std::move(wrapped.first));
        _subscriber.on_completed();
      } else {
        _subscriber.on_error(
            std::make_exception_ptr(GrpcError(wrapped.second)));
      }
    } else {
      _subscriber.on_error(std::make_exception_ptr(GrpcError(_status)));
    }

    delete &_to_delete;
  }

  template <typename InnerStream>
  typename std::enable_if<!StreamTraits<InnerStream>::kRequestStreaming>::type
  invoke(std::unique_ptr<InnerStream> &&stream) {
    stream->Finish(&_response, &_status, this);
  }

  template <typename InnerStream>
  typename std::enable_if<StreamTraits<InnerStream>::kRequestStreaming>::type
  invoke(std::unique_ptr<InnerStream> &&stream) {
    stream->Finish(&_status, this);
  }

  MessageType &response() {
    return _response;
  }

 private:
  MessageType _response;
  rxcpp::subscriber<TransformedMessageType> _subscriber;
  grpc::Status _status;
  OwnerType &_to_delete;
};


/**
 * Streaming version
 */
template <
    typename OwnerType,
    typename MessageType,
    typename Transform,
    typename Stream>
class RxGrpcReader<
    OwnerType, MessageType, Transform, Stream, true> : public RxGrpcTag {
  using Context = typename StreamTraits<Stream>::Context;
  using TransformedMessageType = typename decltype(
      Transform::wrap(std::declval<MessageType>()))::first_type;

 public:
  RxGrpcReader(
      rxcpp::subscriber<TransformedMessageType> &&subscriber,
      Context *context,
      OwnerType *to_delete)
      : _subscriber(std::move(subscriber)),
        _context(*context),
        _to_delete(*to_delete) {}

  void operator()(bool success) override {
    switch (_state) {
      case State::INIT: {
        _state = State::READING_RESPONSE;
        _stream->Read(&_response, this);
        break;
      }
      case State::READING_RESPONSE: {
        if (!success) {
          // We have reached the end of the stream.
          _state = State::FINISHING;
          _stream->Finish(&_status, this);
        } else {
          auto wrapped = Transform::wrap(std::move(_response));
          if (wrapped.second.ok()) {
            _subscriber.on_next(std::move(wrapped.first));
            _stream->Read(&_response, this);
          } else {
            _subscriber.on_error(
                std::make_exception_ptr(GrpcError(wrapped.second)));
            _state = State::READ_FAILURE;
            _context.TryCancel();
            _stream->Finish(&_status, this);
          }
        }
        break;
      }
      case State::FINISHING: {
        if (_status.ok()) {
          _subscriber.on_completed();
        } else {
          _subscriber.on_error(std::make_exception_ptr(GrpcError(_status)));
        }
        delete &_to_delete;
        break;
      }
      case State::READ_FAILURE: {
        delete &_to_delete;
        break;
      }
    }
  }

  void invoke(std::unique_ptr<Stream> &&stream) {
    _stream = std::move(stream);
  }

  MessageType &response() {
    return _response;
  }

 private:
  enum class State {
    INIT,
    READING_RESPONSE,
    FINISHING,
    READ_FAILURE
  };

  State _state = State::INIT;
  MessageType _response;
  rxcpp::subscriber<TransformedMessageType> _subscriber;
  grpc::Status _status;
  std::unique_ptr<Stream> _stream;
  Context &_context;
  OwnerType &_to_delete;
};

}  // namespace detail
}  // namespace shk
