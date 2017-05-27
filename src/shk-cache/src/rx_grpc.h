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

#include <deque>
#include <type_traits>
#include <vector>

#include <grpc++/grpc++.h>
#include <rxcpp/rx.hpp>

#include "grpc_error.h"
#include "rx_grpc_identity_transform.h"
#include "rx_grpc_tag.h"

namespace shk {
namespace detail {

template <
    typename Transform,
    typename ResponseType>
void handleUnaryResponse(
    bool success,
    const grpc::Status &status,
    ResponseType &&response,
    rxcpp::subscriber<typename decltype(Transform::wrap(
        std::declval<ResponseType>()))::first_type> *subscriber) {
  if (!success) {
    subscriber->on_error(std::make_exception_ptr(GrpcError(grpc::Status(
        grpc::UNKNOWN, "The request was interrupted"))));
  } else if (status.ok()) {
    auto wrapped = Transform::wrap(std::move(response));
    if (wrapped.second.ok()) {
      subscriber->on_next(std::move(wrapped.first));
      subscriber->on_completed();
    } else {
      subscriber->on_error(
          std::make_exception_ptr(GrpcError(wrapped.second)));
    }
  } else {
    subscriber->on_error(std::make_exception_ptr(GrpcError(status)));
  }
}

template <
    typename Reader,
    typename ResponseType,
    typename TransformedRequestType,
    typename Transform>
class RxGrpcClientInvocation;

/**
 * Unary client RPC.
 */
template <
    typename ResponseType,
    typename TransformedRequestType,
    typename Transform>
class RxGrpcClientInvocation<
    grpc::ClientAsyncResponseReader<ResponseType>,
    ResponseType,
    TransformedRequestType,
    Transform> : public RxGrpcTag {
 public:
  using TransformedResponseType = typename decltype(
      Transform::wrap(std::declval<ResponseType>()))::first_type;

  RxGrpcClientInvocation(
      const TransformedRequestType &request,
      rxcpp::subscriber<TransformedResponseType> &&subscriber)
      : _request(request),
        _subscriber(std::move(subscriber)) {}

  void operator()(bool success) override {
    handleUnaryResponse<Transform>(
        success, _status, std::move(_response), &_subscriber);
    delete this;
  }

  template <typename Stub, typename RequestType>
  void invoke(
      std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>>
      (Stub::*invoke)(
          grpc::ClientContext *context,
          const RequestType &request,
          grpc::CompletionQueue *cq),
      Stub *stub,
      grpc::CompletionQueue *cq) {
    auto stream = (stub->*invoke)(&_context, Transform::unwrap(_request), cq);
    stream->Finish(&_response, &_status, this);
  }

 private:
  static_assert(
      !std::is_reference<TransformedRequestType>::value,
      "Request type must be held by value");
  TransformedRequestType _request;
  grpc::ClientContext _context;
  ResponseType _response;
  rxcpp::subscriber<TransformedResponseType> _subscriber;
  grpc::Status _status;
};

/**
 * Server streaming.
 */
template <
    typename ResponseType,
    typename TransformedRequestType,
    typename Transform>
class RxGrpcClientInvocation<
    grpc::ClientAsyncReader<ResponseType>,
    ResponseType,
    TransformedRequestType,
    Transform> : public RxGrpcTag {
 public:
  using TransformedResponseType = typename decltype(
      Transform::wrap(std::declval<ResponseType>()))::first_type;

  RxGrpcClientInvocation(
      const TransformedRequestType &request,
      rxcpp::subscriber<TransformedResponseType> &&subscriber)
      : _request(request),
        _subscriber(std::move(subscriber)) {}

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
        delete this;
        break;
      }
      case State::READ_FAILURE: {
        delete this;
        break;
      }
    }
  }

  template <typename Stub, typename RequestType>
  void invoke(
      std::unique_ptr<grpc::ClientAsyncReader<ResponseType>>
      (Stub::*invoke)(
          grpc::ClientContext *context,
          const RequestType &request,
          grpc::CompletionQueue *cq,
          void *tag),
      Stub *stub,
      grpc::CompletionQueue *cq) {
    _stream = (stub->*invoke)(&_context, Transform::unwrap(_request), cq, this);
  }

 private:
  enum class State {
    INIT,
    READING_RESPONSE,
    FINISHING,
    READ_FAILURE
  };

  static_assert(
      !std::is_reference<TransformedRequestType>::value,
      "Request type must be held by value");
  TransformedRequestType _request;
  grpc::ClientContext _context;

  State _state = State::INIT;
  ResponseType _response;
  rxcpp::subscriber<TransformedResponseType> _subscriber;
  grpc::Status _status;
  std::unique_ptr<grpc::ClientAsyncReader<ResponseType>> _stream;
};

/**
 * Client streaming.
 *
 * gRPC supports the use case that the client streams messages, and that the
 * server responds in the middle of the message stream. This implementation only
 * supports reading the response after the client message stream is closed.
 */
template <
    typename RequestType,
    typename ResponseType,
    typename TransformedRequestType,
    typename Transform>
class RxGrpcClientInvocation<
    grpc::ClientAsyncWriter<RequestType>,
    ResponseType,
    TransformedRequestType,
    Transform> : public RxGrpcTag {
 public:
  using TransformedResponseType = typename decltype(
      Transform::wrap(std::declval<ResponseType>()))::first_type;

 public:
  template <typename Observable>
  RxGrpcClientInvocation(
      const Observable &requests,
      rxcpp::subscriber<TransformedResponseType> &&subscriber)
      : _requests(requests.as_dynamic()),
        _subscriber(std::move(subscriber)) {
    static_assert(
        rxcpp::is_observable<Observable>::value,
        "First parameter must be an observable");
  }

  void operator()(bool success) override {
    if (_sent_final_request) {
      if (_request_stream_error) {
        _subscriber.on_error(_request_stream_error);
      } else {
        handleUnaryResponse<Transform>(
            success, _status, std::move(_response), &_subscriber);
      }
      delete this;
    } else {
      if (success) {
        _operation_in_progress = false;
        runEnqueuedOperation();
      } else {
        // This happens when the runloop is shutting down.
        handleUnaryResponse<Transform>(
            success, _status, std::move(_response), &_subscriber);
        delete this;
      }
    }
  }

  template <typename Stub>
  void invoke(
      std::unique_ptr<grpc::ClientAsyncWriter<RequestType>>
      (Stub::*invoke)(
          grpc::ClientContext *context,
          ResponseType *response,
          grpc::CompletionQueue *cq,
          void *tag),
      Stub *stub,
      grpc::CompletionQueue *cq) {
    _stream = (stub->*invoke)(&_context, &_response, cq, this);
    _operation_in_progress = true;

    _requests.subscribe(
        [this](TransformedRequestType request) {
          _enqueued_requests.emplace_back(std::move(request));
          runEnqueuedOperation();
        },
        [this](const std::exception_ptr &error) {
          // This triggers runEnqueuedOperation to Finish the stream.
          _request_stream_error = error;
          _enqueued_writes_done = true;
          runEnqueuedOperation();
        },
        [this]() {
          _enqueued_writes_done = true;
          runEnqueuedOperation();
        });
  }

 private:
  void runEnqueuedOperation() {
    if (_operation_in_progress) {
      return;
    }
    if (!_enqueued_requests.empty()) {
      _operation_in_progress = true;
      _stream->Write(
          Transform::unwrap(std::move(_enqueued_requests.front())), this);
      _enqueued_requests.pop_front();
    } else if (_enqueued_writes_done) {
      _enqueued_writes_done = false;
      _enqueued_finish = true;
      _operation_in_progress = true;
      _stream->WritesDone(this);
    } else if (_enqueued_finish) {
      _enqueued_finish = false;
      _operation_in_progress = true;

      // Must be done before the call to Finish because it's not safe to do
      // anything after that call; gRPC could invoke the callback immediately
      // on another thread, which could delete this.
      _sent_final_request = true;

      _stream->Finish(&_status, this);
    }
  }

  rxcpp::observable<TransformedRequestType> _requests;
  ResponseType _response;
  std::unique_ptr<grpc::ClientAsyncWriter<RequestType>> _stream;
  grpc::ClientContext _context;
  rxcpp::subscriber<TransformedResponseType> _subscriber;

  std::exception_ptr _request_stream_error;
  bool _sent_final_request = false;
  bool _operation_in_progress = false;

  // Because we don't have backpressure we need an unbounded buffer here :-(
  std::deque<TransformedRequestType> _enqueued_requests;
  bool _enqueued_writes_done = false;
  bool _enqueued_finish = false;
  grpc::Status _status;
};

/**
 * Bidi.
 */
template <
    typename RequestType,
    typename ResponseType,
    typename TransformedRequestType,
    typename Transform>
class RxGrpcClientInvocation<
    grpc::ClientAsyncReaderWriter<RequestType, ResponseType>,
    ResponseType,
    TransformedRequestType,
    Transform> : public RxGrpcTag {
 public:
  using TransformedResponseType = typename decltype(
      Transform::wrap(std::declval<ResponseType>()))::first_type;

 private:
  /**
   * Bidi streaming requires separate tags for reading and writing (since they
   * can happen simultaneously). The purpose of this class is to be the other
   * tag. It's used for reading.
   */
  class Reader : public RxGrpcTag {
   public:
    Reader(
        const std::function<void ()> &shutdown,
        rxcpp::subscriber<TransformedResponseType> &&subscriber)
        : _shutdown(shutdown),
          _subscriber(std::move(subscriber)) {}

    void invoke(
        grpc::ClientAsyncReaderWriter<RequestType, ResponseType> *stream) {
      _stream = stream;
      _stream->Read(&_response, this);
    }

    /**
     * Try to signal an error to the subscriber. If subscriber stream has
     * already been closed, this is a no-op.
     */
    void onError(const std::exception_ptr &error) {
      _error = error;
    }

    /**
     * Should be called by _shutdown when the stream is actually about to be
     * destroyed. We don't call on_error or on_completed on the subscriber until
     * then because it's not until both the read stream and the write stream
     * have finished that it is known for sure that there was no error.
     */
    void finish(const grpc::Status &status) {
      if (!status.ok()) {
        _subscriber.on_error(std::make_exception_ptr(GrpcError(status)));
      } else if (_error) {
        _subscriber.on_error(_error);
      } else {
        _subscriber.on_completed();
      }
    }

    void operator()(bool success) override {
      if (!success || _error) {
        // We have reached the end of the stream.
        _shutdown();
      } else {
        auto wrapped = Transform::wrap(std::move(_response));
        if (wrapped.second.ok()) {
          _subscriber.on_next(std::move(wrapped.first));
          _stream->Read(&_response, this);
        } else {
          _error = std::make_exception_ptr(GrpcError(wrapped.second));
          _shutdown();
        }
      }
    }

   private:
    std::exception_ptr _error;
    // Should be called when the read stream is finished. Care must be taken so
    // that this is not called when there is an outstanding async operation.
    std::function<void ()> _shutdown;
    grpc::ClientAsyncReaderWriter<RequestType, ResponseType> *_stream = nullptr;
    rxcpp::subscriber<TransformedResponseType> _subscriber;
    ResponseType _response;
  };

 public:
  template <typename Observable>
  RxGrpcClientInvocation(
      const Observable &requests,
      rxcpp::subscriber<TransformedResponseType> &&subscriber)
      : _reader(
            [this] { _reader_done = true; tryShutdown(); },
            std::move(subscriber)),
        _requests(requests.as_dynamic()) {
    static_assert(
        rxcpp::is_observable<Observable>::value,
        "First parameter must be an observable");
  }

  void operator()(bool success) override {
    if (_sent_final_request) {
      _writer_done = true;
      tryShutdown();
    } else {
      if (success) {
        _operation_in_progress = false;
        runEnqueuedOperation();
      } else {
        // This happens when the runloop is shutting down.
        _writer_done = true;
        tryShutdown();
      }
    }
  }

  template <typename Stub>
  void invoke(
      std::unique_ptr<grpc::ClientAsyncReaderWriter<RequestType, ResponseType>>
      (Stub::*invoke)(
          grpc::ClientContext *context,
          grpc::CompletionQueue *cq,
          void *tag),
      Stub *stub,
      grpc::CompletionQueue *cq) {
    _stream = (stub->*invoke)(&_context, cq, this);
    _reader.invoke(_stream.get());
    _operation_in_progress = true;

    _requests.subscribe(
        [this](TransformedRequestType request) {
          _enqueued_requests.emplace_back(std::move(request));
          runEnqueuedOperation();
        },
        [this](const std::exception_ptr &error) {
          _reader.onError(error);
          _enqueued_writes_done = true;
          runEnqueuedOperation();
        },
        [this]() {
          _enqueued_writes_done = true;
          runEnqueuedOperation();
        });
  }

 private:
  enum class State {
    SENDING_REQUESTS,
    TO_SEND_WRITES_DONE,
    TO_SEND_FINISH,
    DONE
  };

  void runEnqueuedOperation() {
    if (_operation_in_progress) {
      return;
    }
    if (!_enqueued_requests.empty()) {
      _operation_in_progress = true;
      _stream->Write(
          Transform::unwrap(std::move(_enqueued_requests.front())), this);
      _enqueued_requests.pop_front();
    } else if (_enqueued_writes_done) {
      _enqueued_writes_done = false;
      _enqueued_finish = true;
      _operation_in_progress = true;
      _stream->WritesDone(this);
    } else if (_enqueued_finish) {
      _enqueued_finish = false;
      _operation_in_progress = true;

      // Must be done before the call to Finish because it's not safe to do
      // anything after that call; gRPC could invoke the callback immediately
      // on another thread, which could delete this.
      _sent_final_request = true;

      _stream->Finish(&_status, this);
    }
  }

  void tryShutdown() {
    if (_writer_done && _reader_done) {
      _reader.finish(_status);
      delete this;
    }
  }

  Reader _reader;
  bool _reader_done = false;

  rxcpp::observable<TransformedRequestType> _requests;
  ResponseType _response;
  std::unique_ptr<
      grpc::ClientAsyncReaderWriter<RequestType, ResponseType>> _stream;
  grpc::ClientContext _context;

  bool _sent_final_request = false;
  bool _operation_in_progress = false;
  bool _writer_done = false;

  // Because we don't have backpressure we need an unbounded buffer here :-(
  std::deque<TransformedRequestType> _enqueued_requests;
  bool _enqueued_writes_done = false;
  bool _enqueued_finish = false;
  grpc::Status _status;
};

/**
 * For server requests with a non-streaming response.
 */
template <
    typename Service,
    typename RequestType,
    // grpc::ServerAsyncResponseWriter<ResponseType> or
    // grpc::ServerAsyncWriter<ResponseType>
    typename Stream>
using RequestMethod = void (Service::*)(
    grpc::ServerContext *context,
    RequestType *request,
    Stream *stream,
    grpc::CompletionQueue *new_call_cq,
    grpc::ServerCompletionQueue *notification_cq,
    void *tag);

/**
 * For server requests with a streaming response.
 */
template <
    typename Service,
    // grpc::ServerAsyncReader<ResponseType, RequestType> or
    // grpc::ServerAsyncReaderWriter<ResponseType, RequestType>
    typename Stream>
using StreamingRequestMethod = void (Service::*)(
    grpc::ServerContext *context,
    Stream *stream,
    grpc::CompletionQueue *new_call_cq,
    grpc::ServerCompletionQueue *notification_cq,
    void *tag);

/**
 * Group of typedefs related to a server-side invocation, to avoid having to
 * pass around tons and tons of template parameters everywhere.
 */
template <
    // grpc::ServerAsyncResponseWriter<ResponseType> (non-streaming) or
    // grpc::ServerAsyncWriter<ResponseType> (streaming response) or
    // grpc::ServerAsyncReader<ResponseType, RequestType> (streaming request) or
    // grpc::ServerAsyncReaderWriter<ResponseType, RequestType> (bidi streaming)
    typename StreamType,
    // Generated service class
    typename ServiceType,
    typename ResponseType,
    typename RequestType,
    typename TransformType,
    typename Callback>
class ServerCallTraits {
 public:
  using Stream = StreamType;
  using Service = ServiceType;
  using Response = ResponseType;
  using Request = RequestType;
  using Transform = TransformType;

  using TransformedRequest =
      typename decltype(
          Transform::wrap(std::declval<RequestType>()))::first_type;
};


template <typename Stream, typename ServerCallTraits, typename Callback>
class RxGrpcServerInvocation;

/**
 * Unary server RPC.
 */
template <typename ResponseType, typename ServerCallTraits, typename Callback>
class RxGrpcServerInvocation<
    grpc::ServerAsyncResponseWriter<ResponseType>,
    ServerCallTraits,
    Callback> : public RxGrpcTag {
  using Stream = grpc::ServerAsyncResponseWriter<ResponseType>;
  using Service = typename ServerCallTraits::Service;
  using Transform = typename ServerCallTraits::Transform;
  using TransformedRequest = typename ServerCallTraits::TransformedRequest;

  using ResponseObservable =
      decltype(std::declval<Callback>()(std::declval<TransformedRequest>()));
  using TransformedResponse = typename ResponseObservable::value_type;

  using Method = RequestMethod<
      Service, typename ServerCallTraits::Request, Stream>;

 public:
  static void request(
      GrpcErrorHandler error_handler,
      Method method,
      Callback &&callback,
      Service *service,
      grpc::ServerCompletionQueue *cq) {
    auto invocation = new RxGrpcServerInvocation(
        error_handler, method, std::move(callback), service, cq);

    (service->*method)(
        &invocation->_context,
        &invocation->_request,
        &invocation->_stream,
        cq,
        cq,
        invocation);
  }

  void operator()(bool success) {
    if (!success) {
      // This happens when the server is shutting down.
      delete this;
      return;
    }

    if (_awaiting_request) {
      // The server has just received a request. Handle it.

      auto wrapped_request = ServerCallTraits::Transform::wrap(
          std::move(_request));
      auto values = wrapped_request.second.ok() ?
          _callback(std::move(wrapped_request.first)).as_dynamic() :
          rxcpp::observable<>::error<TransformedResponse>(
              GrpcError(wrapped_request.second)).as_dynamic();

      // Request the a new request, so that the server is always waiting for
      // one. This is done after the callback (because this steals it) but
      // before the subscribe call because that could tell gRPC to respond,
      // after which it's not safe to do anything with `this` anymore.
      issueNewServerRequest(std::move(_callback));

      _awaiting_request = false;

      values.subscribe(
          [this](TransformedResponse response) {
            _num_responses++;
            _response = std::move(response);
          },
          [this](const std::exception_ptr &error) {
            _stream.FinishWithError(exceptionToStatus(error), this);
          },
          [this]() {
            if (_num_responses == 1) {
              _stream.Finish(
                  Transform::unwrap(_response), grpc::Status::OK, this);
            } else {
              const auto *error_message =
                  _num_responses == 0 ? "No response" : "Too many responses";
              _stream.FinishWithError(
                  grpc::Status(grpc::StatusCode::INTERNAL, error_message),
                  this);
            }
          });
    } else {
      // The server has now successfully sent a response. Clean up.
      delete this;
    }
  }

 private:
  RxGrpcServerInvocation(
      GrpcErrorHandler error_handler,
      Method method,
      Callback &&callback,
      Service *service,
      grpc::ServerCompletionQueue *cq)
      : _error_handler(error_handler),
        _method(method),
        _callback(std::move(callback)),
        _service(*service),
        _cq(*cq),
        _stream(&_context) {}

  void issueNewServerRequest(Callback &&callback) {
    // Take callback as an rvalue parameter to make it obvious that we steal it.
    request(
        _error_handler,
        _method,
        std::move(callback),  // Reuse the callback functor, don't copy
        &_service,
        &_cq);
  }

  bool _awaiting_request = true;
  GrpcErrorHandler _error_handler;
  Method _method;
  Callback _callback;
  Service &_service;
  grpc::ServerCompletionQueue &_cq;
  grpc::ServerContext _context;
  typename ServerCallTraits::Request _request;
  Stream _stream;
  int _num_responses = 0;
  TransformedResponse _response;
};

/**
 * Server streaming.
 */
template <typename ResponseType, typename ServerCallTraits, typename Callback>
class RxGrpcServerInvocation<
    grpc::ServerAsyncWriter<ResponseType>,
    ServerCallTraits,
    Callback> : public RxGrpcTag {
  using Stream = grpc::ServerAsyncWriter<ResponseType>;
  using Service = typename ServerCallTraits::Service;
  using Transform = typename ServerCallTraits::Transform;
  using TransformedRequest = typename ServerCallTraits::TransformedRequest;

  using ResponseObservable =
      decltype(std::declval<Callback>()(std::declval<TransformedRequest>()));
  using TransformedResponse = typename ResponseObservable::value_type;

  using Method = RequestMethod<
      Service, typename ServerCallTraits::Request, Stream>;

 public:
  static void request(
      GrpcErrorHandler error_handler,
      Method method,
      Callback &&callback,
      Service *service,
      grpc::ServerCompletionQueue *cq) {
    auto invocation = new RxGrpcServerInvocation(
        error_handler, method, std::move(callback), service, cq);

    (service->*method)(
        &invocation->_context,
        &invocation->_request,
        &invocation->_stream,
        cq,
        cq,
        invocation);
  }

  void operator()(bool success) {
    if (!success) {
      // This happens when the server is shutting down.
      delete this;
      return;
    }

    switch (_state) {
      case State::AWAITING_REQUEST: {
        // The server has just received a request. Handle it.
        _state = State::AWAITING_RESPONSE;

        auto wrapped_request = ServerCallTraits::Transform::wrap(
            std::move(_request));
        auto values = wrapped_request.second.ok() ?
            _callback(std::move(wrapped_request.first)).as_dynamic() :
            rxcpp::observable<>::error<TransformedResponse>(
                GrpcError(wrapped_request.second)).as_dynamic();

        // Request the a new request, so that the server is always waiting for
        // one. This is done after the callback (because this steals it) but
        // before the subscribe call because that could tell gRPC to respond,
        // after which it's not safe to do anything with `this` anymore.
        issueNewServerRequest(std::move(_callback));

        values.subscribe(
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

        break;
      }
      case State::AWAITING_RESPONSE:
      case State::SENDING_RESPONSE: {
        _state = State::AWAITING_RESPONSE;
        runEnqueuedOperation();
        break;
      }
      case State::SENT_FINAL_RESPONSE: {
        delete this;
        break;
      }
    }
  }

 private:
  enum class State {
    AWAITING_REQUEST,
    AWAITING_RESPONSE,
    SENDING_RESPONSE,
    SENT_FINAL_RESPONSE
  };

  RxGrpcServerInvocation(
      GrpcErrorHandler error_handler,
      Method method,
      Callback &&callback,
      Service *service,
      grpc::ServerCompletionQueue *cq)
      : _error_handler(error_handler),
        _method(method),
        _callback(std::move(callback)),
        _service(*service),
        _cq(*cq),
        _stream(&_context) {}

  void issueNewServerRequest(Callback &&callback) {
    // Take callback as an rvalue parameter to make it obvious that we steal it.
    request(
        _error_handler,
        _method,
        std::move(callback),  // Reuse the callback functor, don't copy
        &_service,
        &_cq);
  }

  void runEnqueuedOperation() {
    if (_state != State::AWAITING_RESPONSE) {
      return;
    }
    if (!_enqueued_responses.empty()) {
      _state = State::SENDING_RESPONSE;
      _stream.Write(
          Transform::unwrap(std::move(_enqueued_responses.front())), this);
      _enqueued_responses.pop_front();
    } else if (_enqueued_finish) {
      _enqueued_finish = false;
      _state = State::SENT_FINAL_RESPONSE;
      _stream.Finish(_enqueued_finish_status, this);
    }
  }

  State _state = State::AWAITING_REQUEST;
  bool _enqueued_finish = false;
  grpc::Status _enqueued_finish_status;
  std::deque<TransformedResponse> _enqueued_responses;

  GrpcErrorHandler _error_handler;
  Method _method;
  Callback _callback;
  Service &_service;
  grpc::ServerCompletionQueue &_cq;
  grpc::ServerContext _context;
  typename ServerCallTraits::Request _request;
  Stream _stream;
};

/**
 * Client streaming.
 */
template <
    typename ResponseType,
    typename RequestType,
    typename ServerCallTraits,
    typename Callback>
class RxGrpcServerInvocation<
    grpc::ServerAsyncReader<ResponseType, RequestType>,
    ServerCallTraits,
    Callback> : public RxGrpcTag {
  using Stream = grpc::ServerAsyncReader<ResponseType, RequestType>;
  using Service = typename ServerCallTraits::Service;
  using Transform = typename ServerCallTraits::Transform;
  using TransformedRequest = typename ServerCallTraits::TransformedRequest;

  using ResponseObservable =
      decltype(std::declval<Callback>()(
          std::declval<rxcpp::observable<TransformedRequest>>()));
  using TransformedResponse = typename ResponseObservable::value_type;

  using Method = StreamingRequestMethod<Service, Stream>;

 public:
  static void request(
      GrpcErrorHandler error_handler,
      Method method,
      Callback &&callback,
      Service *service,
      grpc::ServerCompletionQueue *cq) {
    auto invocation = new RxGrpcServerInvocation(
        error_handler, method, std::move(callback), service, cq);

    (service->*method)(
        &invocation->_context,
        &invocation->_reader,
        cq,
        cq,
        invocation);
  }

  void operator()(bool success) {
    switch (_state) {
      case State::INIT: {
        if (!success) {
          delete this;
        } else {
          // Need to set _state before the call to init, in case it moves on to
          // the State::REQUESTED_DATA state immediately.
          _state = State::INITIALIZED;
          init();
        }
        break;
      }
      case State::INITIALIZED: {
        abort();  // Should not get here
        break;
      }
      case State::REQUESTED_DATA: {
        if (success) {
          auto wrapped = Transform::wrap(std::move(_request));
          if (wrapped.second.ok()) {
            _subscriber->on_next(std::move(wrapped.first));
            _reader.Read(&_request, this);
          } else {
            _subscriber->on_error(
                std::make_exception_ptr(GrpcError(wrapped.second)));

            _state = State::SENT_RESPONSE;
            _reader.FinishWithError(wrapped.second, this);
          }
        } else {
          // The client has stopped sending requests.
          _subscriber->on_completed();
          _state = State::STREAM_ENDED;
          trySendResponse();
        }
        break;
      }
      case State::STREAM_ENDED: {
        abort();  // Should not get here
        break;
      }
      case State::SENT_RESPONSE: {
        // success == false implies that the server is shutting down. It doesn't
        // change what needs to be done here.
        delete this;
        break;
      }
    }
  }

 private:
  enum class State {
    INIT,
    INITIALIZED,
    REQUESTED_DATA,
    STREAM_ENDED,
    SENT_RESPONSE
  };

  RxGrpcServerInvocation(
      GrpcErrorHandler error_handler,
      Method method,
      Callback &&callback,
      Service *service,
      grpc::ServerCompletionQueue *cq)
      : _error_handler(error_handler),
        _method(method),
        _callback(std::move(callback)),
        _service(*service),
        _cq(*cq),
        _reader(&_context) {}

  void init() {
    auto response = _callback(rxcpp::observable<>::create<TransformedRequest>(
        [this](rxcpp::subscriber<TransformedRequest> subscriber) {
      if (_subscriber) {
        throw std::logic_error(
            "Can't subscribe to this observable more than once");
      }
      _subscriber.reset(
          new rxcpp::subscriber<TransformedRequest>(std::move(subscriber)));

      _state = State::REQUESTED_DATA;
      _reader.Read(&_request, this);
    }));

    static_assert(
        rxcpp::is_observable<decltype(response)>::value,
        "Callback return type must be observable");
    response.subscribe(
        [this](TransformedResponse response) {
          _response = std::move(response);
          _num_responses++;
        },
        [this](const std::exception_ptr &error) {
          _response_error = error;
          _finished = true;
          trySendResponse();
        },
        [this]() {
          _finished = true;
          trySendResponse();
        });

    // Request the a new request, so that the server is always waiting for
    // one.
    request(
        _error_handler,
        _method,
        std::move(_callback),  // Reuse the callback functor, don't copy
        &_service,
        &_cq);
  }

  void issueNewServerRequest(std::unique_ptr<Callback> &&callback) {
  }

  void trySendResponse() {
    if (_finished && _state == State::STREAM_ENDED) {
      _state = State::SENT_RESPONSE;
      if (_response_error) {
        _reader.FinishWithError(exceptionToStatus(_response_error), this);
      } else if (_num_responses == 1) {
        _reader.Finish(
            Transform::unwrap(_response),
            grpc::Status::OK,
            this);
      } else {
        const auto *error_message =
            _num_responses == 0 ? "No response" : "Too many responses";
        _reader.FinishWithError(
            grpc::Status(grpc::StatusCode::INTERNAL, error_message),
            this);
      }
    }
  }

  std::unique_ptr<rxcpp::subscriber<TransformedRequest>> _subscriber;
  State _state = State::INIT;
  GrpcErrorHandler _error_handler;
  Method _method;
  Callback _callback;
  Service &_service;
  grpc::ServerCompletionQueue &_cq;
  grpc::ServerContext _context;
  typename ServerCallTraits::Request _request;
  grpc::ServerAsyncReader<ResponseType, RequestType> _reader;

  TransformedResponse _response;
  int _num_responses = 0;

  std::exception_ptr _response_error;
  bool _finished = false;
};

/**
 * Bidi streaming.
 */
template <
    typename ResponseType,
    typename RequestType,
    typename ServerCallTraits,
    typename Callback>
class RxGrpcServerInvocation<
    grpc::ServerAsyncReaderWriter<ResponseType, RequestType>,
    ServerCallTraits,
    Callback> : public RxGrpcTag {
  using Stream = grpc::ServerAsyncReaderWriter<ResponseType, RequestType>;
  using Service = typename ServerCallTraits::Service;
  using Transform = typename ServerCallTraits::Transform;
  using TransformedRequest = typename ServerCallTraits::TransformedRequest;

  using ResponseObservable =
      decltype(std::declval<Callback>()(
          std::declval<rxcpp::observable<TransformedRequest>>()));
  using TransformedResponse = typename ResponseObservable::value_type;

  using Method = StreamingRequestMethod<Service, Stream>;

  /**
   * Bidi streaming requires separate tags for reading and writing (since they
   * can happen simultaneously). The purpose of this class is to be the other
   * tag. It's used for writing.
   */
  class Writer : public RxGrpcTag {
   public:
    Writer(
        const std::function<void ()> shutdown,
        grpc::ServerContext *context,
        grpc::ServerAsyncReaderWriter<ResponseType, RequestType> *stream)
        : _shutdown(shutdown),
          _context(*context),
          _stream(*stream) {}

    template <typename SourceOperator>
    void subscribe(
        const rxcpp::observable<
            TransformedResponse, SourceOperator> &observable) {
      observable.subscribe(
          [this](TransformedResponse response) {
            _enqueued_responses.emplace_back(std::move(response));
            runEnqueuedOperation();
          },
          [this](const std::exception_ptr &error) {
            onError(error);
          },
          [this]() {
            _enqueued_finish = true;
            runEnqueuedOperation();
          });
    }

    /**
     * Try to end the write stream with an error. If the write stream has
     * already finished, this is a no-op.
     */
    void onError(const std::exception_ptr &error) {
      _status = exceptionToStatus(error);
      _enqueued_finish = true;
      runEnqueuedOperation();
    }

    void operator()(bool success) override {
      if (_sent_final_request) {
        // Nothing more to write.
        _shutdown();
      } else {
        if (success) {
          _operation_in_progress = false;
          runEnqueuedOperation();
        } else {
          // This happens when the runloop is shutting down.
          _shutdown();
        }
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
      } else if (_enqueued_finish && !_sent_final_request) {
        _enqueued_finish = false;
        _operation_in_progress = true;

        // Must be done before the call to Finish because it's not safe to do
        // anything after that call; gRPC could invoke the callback immediately
        // on another thread, which could delete this.
        _sent_final_request = true;

        _stream.Finish(_status, this);
      }
    }

    std::function<void ()> _shutdown;
    // Because we don't have backpressure we need an unbounded buffer here :-(
    std::deque<TransformedResponse> _enqueued_responses;
    bool _enqueued_finish = false;
    bool _operation_in_progress = false;
    bool _sent_final_request = false;
    grpc::ServerContext &_context;
    grpc::ServerAsyncReaderWriter<ResponseType, RequestType> &_stream;
    grpc::Status _status;
  };

 public:
  static void request(
      GrpcErrorHandler error_handler,
      Method method,
      Callback &&callback,
      Service *service,
      grpc::ServerCompletionQueue *cq) {
    auto invocation = new RxGrpcServerInvocation(
        error_handler, method, std::move(callback), service, cq);

    (service->*method)(
        &invocation->_context,
        &invocation->_stream,
        cq,
        cq,
        invocation);
  }

  void operator()(bool success) {
    switch (_state) {
      case State::INIT: {
        if (!success) {
          delete this;
        } else {
          // Need to set _state before the call to init, in case it moves on to
          // the State::REQUESTED_DATA state immediately.
          _state = State::INITIALIZED;
          init();
        }
        break;
      }
      case State::INITIALIZED: {
        abort();  // Should not get here
        break;
      }
      case State::REQUESTED_DATA: {
        if (success) {
          auto wrapped = Transform::wrap(std::move(_request));
          if (wrapped.second.ok()) {
            _subscriber->on_next(std::move(wrapped.first));
            _stream.Read(&_request, this);
          } else {
            auto error = std::make_exception_ptr(GrpcError(wrapped.second));
            _subscriber->on_error(error);

            _writer.onError(error);
            _state = State::READ_STREAM_ENDED;
          }
        } else {
          // The client has stopped sending requests.
          _subscriber->on_completed();
          _state = State::READ_STREAM_ENDED;
          tryShutdown();
        }
        break;
      }
      case State::READ_STREAM_ENDED: {
        abort();  // Should not get here
        break;
      }
    }
  }

 private:
  enum class State {
    INIT,
    INITIALIZED,
    REQUESTED_DATA,
    READ_STREAM_ENDED
  };

  RxGrpcServerInvocation(
      GrpcErrorHandler error_handler,
      Method method,
      Callback &&callback,
      Service *service,
      grpc::ServerCompletionQueue *cq)
      : _error_handler(error_handler),
        _method(method),
        _callback(std::move(callback)),
        _service(*service),
        _cq(*cq),
        _stream(&_context),
        _writer(
            [this] { _write_stream_ended = true; tryShutdown(); },
            &_context,
            &_stream) {}

  void tryShutdown() {
    if (_state == State::READ_STREAM_ENDED && _write_stream_ended) {
      // Only delete this when both the read stream and the write stream have
      // finished.
      delete this;
    }
  }

  void init() {
    auto response = _callback(rxcpp::observable<>::create<TransformedRequest>(
        [this](rxcpp::subscriber<TransformedRequest> subscriber) {
      if (_subscriber) {
        throw std::logic_error(
            "Can't subscribe to this observable more than once");
      }
      _subscriber.reset(
          new rxcpp::subscriber<TransformedRequest>(std::move(subscriber)));

      _state = State::REQUESTED_DATA;
      _stream.Read(&_request, this);
    }));

    static_assert(
        rxcpp::is_observable<decltype(response)>::value,
        "Callback return type must be observable");
    _writer.subscribe(response);

    request(
        _error_handler,
        _method,
        std::move(_callback),  // Reuse the callback functor, don't copy
        &_service,
        &_cq);
  }

  std::unique_ptr<rxcpp::subscriber<TransformedRequest>> _subscriber;
  State _state = State::INIT;
  GrpcErrorHandler _error_handler;
  Method _method;
  Callback _callback;
  Service &_service;
  grpc::ServerCompletionQueue &_cq;
  grpc::ServerContext _context;
  typename ServerCallTraits::Request _request;
  grpc::ServerAsyncReaderWriter<ResponseType, RequestType> _stream;
  bool _write_stream_ended = false;
  Writer _writer;

  TransformedResponse _response;
  std::exception_ptr _response_error;
};

class InvocationRequester {
 public:
  virtual ~InvocationRequester() = default;

  virtual void requestInvocation(
      GrpcErrorHandler error_handler,
      grpc::ServerCompletionQueue *cq) = 0;
};

template <
    // RequestMethod<Service, RequestType, Stream> or
    // StreamingRequestMethod<Service, Stream>
    typename Method,
    typename ServerCallTraits,
    typename Callback>
class RxGrpcServerInvocationRequester : public InvocationRequester {
  using Service = typename ServerCallTraits::Service;

 public:
  RxGrpcServerInvocationRequester(
      Method method, Callback &&callback, Service *service)
      : _method(method), _callback(std::move(callback)), _service(*service) {}

  void requestInvocation(
      GrpcErrorHandler error_handler,
      grpc::ServerCompletionQueue *cq) override {
    using ServerInvocation = RxGrpcServerInvocation<
        typename ServerCallTraits::Stream,
        ServerCallTraits,
        Callback>;
    ServerInvocation::request(
        error_handler, _method, Callback(_callback), &_service, cq);
  }

 private:
  Method _method;
  Callback _callback;
  Service &_service;
};

}  // namespace detail

template <typename Stub, typename Transform>
class RxGrpcServiceClient {
 public:
  RxGrpcServiceClient(std::unique_ptr<Stub> &&stub, grpc::CompletionQueue *cq)
      : _stub(std::move(stub)), _cq(*cq) {}

  /**
   * Unary rpc.
   */
  template <typename ResponseType, typename TransformedRequestType>
  rxcpp::observable<
      typename detail::RxGrpcClientInvocation<
          grpc::ClientAsyncResponseReader<ResponseType>,
          ResponseType,
          TransformedRequestType,
          Transform>::TransformedResponseType>
  invoke(
      std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>>
      (Stub::*invoke)(
          grpc::ClientContext *context,
          const decltype(Transform::unwrap(
              std::declval<TransformedRequestType>())) &request,
          grpc::CompletionQueue *cq),
      const TransformedRequestType &request,
      grpc::ClientContext &&context = grpc::ClientContext()) {
    return invokeImpl<
        grpc::ClientAsyncResponseReader<ResponseType>,
        ResponseType,
        TransformedRequestType>(
            invoke,
            request,
            std::move(context));
  }

  /**
   * Server streaming.
   */
  template <typename ResponseType, typename TransformedRequestType>
  rxcpp::observable<
      typename detail::RxGrpcClientInvocation<
          grpc::ClientAsyncReader<ResponseType>,
          ResponseType,
          TransformedRequestType,
          Transform>::TransformedResponseType>
  invoke(
      std::unique_ptr<grpc::ClientAsyncReader<ResponseType>>
      (Stub::*invoke)(
          grpc::ClientContext *context,
          const decltype(Transform::unwrap(
              std::declval<TransformedRequestType>())) &request,
          grpc::CompletionQueue *cq,
          void *tag),
      const TransformedRequestType &request,
      grpc::ClientContext &&context = grpc::ClientContext()) {
    return invokeImpl<
        grpc::ClientAsyncReader<ResponseType>,
        ResponseType,
        TransformedRequestType>(
            invoke,
            request,
            std::move(context));
  }

  /**
   * Client streaming.
   */
  template <
      typename RequestType,
      typename ResponseType,
      typename TransformedRequestType,
      typename SourceOperator>
  rxcpp::observable<
      typename detail::RxGrpcClientInvocation<
          grpc::ClientAsyncWriter<RequestType>,
          ResponseType,
          TransformedRequestType,
          Transform>::TransformedResponseType>
  invoke(
      std::unique_ptr<grpc::ClientAsyncWriter<RequestType>>
      (Stub::*invoke)(
          grpc::ClientContext *context,
          ResponseType *response,
          grpc::CompletionQueue *cq,
          void *tag),
      const rxcpp::observable<TransformedRequestType, SourceOperator> &requests,
      grpc::ClientContext &&context = grpc::ClientContext()) {
    return invokeImpl<
        grpc::ClientAsyncWriter<RequestType>,
        ResponseType,
        TransformedRequestType>(
            invoke,
            requests,
            std::move(context));
  }

  /**
   * Bidi streaming
   */
  template <
      typename RequestType,
      typename ResponseType,
      typename TransformedRequestType,
      typename SourceOperator>
  rxcpp::observable<
      typename detail::RxGrpcClientInvocation<
          grpc::ClientAsyncReaderWriter<RequestType, ResponseType>,
          ResponseType,
          TransformedRequestType,
          Transform>::TransformedResponseType>
  invoke(
      std::unique_ptr<grpc::ClientAsyncReaderWriter<RequestType, ResponseType>>
      (Stub::*invoke)(
          grpc::ClientContext *context,
          grpc::CompletionQueue *cq,
          void *tag),
      const rxcpp::observable<TransformedRequestType, SourceOperator> &requests,
      grpc::ClientContext &&context = grpc::ClientContext()) {
    return invokeImpl<
        grpc::ClientAsyncReaderWriter<RequestType, ResponseType>,
        ResponseType,
        TransformedRequestType>(
            invoke,
            requests,
            std::move(context));
  }

 private:
  template <
      typename Reader,
      typename ResponseType,
      typename TransformedRequestType,
      typename RequestOrObservable,
      typename Invoke>
  rxcpp::observable<
      typename detail::RxGrpcClientInvocation<
          Reader,
          ResponseType,
          TransformedRequestType,
          Transform>::TransformedResponseType>
  invokeImpl(
      Invoke invoke,
      const RequestOrObservable &request_or_observable,
      grpc::ClientContext &&context = grpc::ClientContext()) {

    using ClientInvocation =
        detail::RxGrpcClientInvocation<
            Reader, ResponseType, TransformedRequestType, Transform>;
    using TransformedResponseType =
        typename ClientInvocation::TransformedResponseType;

    return rxcpp::observable<>::create<TransformedResponseType>([
        this, request_or_observable, invoke](
            rxcpp::subscriber<TransformedResponseType> subscriber) {

      auto call = new ClientInvocation(
          request_or_observable, std::move(subscriber));
      call->invoke(invoke, _stub.get(), &_cq);
    });
  }

  std::unique_ptr<Stub> _stub;
  grpc::CompletionQueue &_cq;
};

class RxGrpcServer {
  using ServiceRef = std::unique_ptr<void, std::function<void (void *)>>;
  using Services = std::vector<ServiceRef>;
 public:
  RxGrpcServer(
      Services &&services,
      std::unique_ptr<grpc::ServerCompletionQueue> &&cq,
      std::unique_ptr<grpc::Server> &&server)
      : _services(std::move(services)),
        _cq(std::move(cq)),
        _server(std::move(server)) {}

  RxGrpcServer(RxGrpcServer &&) = default;
  RxGrpcServer &operator=(RxGrpcServer &&) = default;

  ~RxGrpcServer() {
    shutdown();
  }

  class Builder {
   public:
    template <typename Service>
    class ServiceBuilder {
     public:
      /**
       * The pointers passed to the constructor are not Transformed by this
       * class; they need to stay alive for as long as this object exists.
       */
      ServiceBuilder(
          Service *service,
          std::vector<std::unique_ptr<detail::InvocationRequester>> *requesters)
          : _service(*service),
            _invocation_requesters(*requesters) {}

      // Unary RPC
      template <
          typename Transform = detail::RxGrpcIdentityTransform,
          typename InnerService,
          typename ResponseType,
          typename RequestType,
          typename Callback>
      ServiceBuilder &registerMethod(
          detail::RequestMethod<
              InnerService,
              RequestType,
              grpc::ServerAsyncResponseWriter<ResponseType>> method,
          Callback &&callback) {
        registerMethodImpl<
            detail::ServerCallTraits<
                grpc::ServerAsyncResponseWriter<ResponseType>,
                Service,
                ResponseType,
                RequestType,
                Transform,
                Callback>>(
                    method, std::forward<Callback>(callback));

        return *this;
      }

      // Server streaming
      template <
          typename Transform = detail::RxGrpcIdentityTransform,
          typename InnerService,
          typename ResponseType,
          typename RequestType,
          typename Callback>
      ServiceBuilder &registerMethod(
          detail::RequestMethod<
              InnerService,
              RequestType,
              grpc::ServerAsyncWriter<ResponseType>> method,
          Callback &&callback) {
        registerMethodImpl<
            detail::ServerCallTraits<
                grpc::ServerAsyncWriter<ResponseType>,
                Service,
                ResponseType,
                RequestType,
                Transform,
                Callback>>(
                    method, std::forward<Callback>(callback));

        return *this;
      }

      // Client streaming
      template <
          typename Transform = detail::RxGrpcIdentityTransform,
          typename InnerService,
          typename ResponseType,
          typename RequestType,
          typename Callback>
      ServiceBuilder &registerMethod(
          detail::StreamingRequestMethod<
              InnerService,
              grpc::ServerAsyncReader<ResponseType, RequestType>> method,
          Callback &&callback) {
        registerMethodImpl<
            detail::ServerCallTraits<
                grpc::ServerAsyncReader<ResponseType, RequestType>,
                Service,
                ResponseType,
                RequestType,
                Transform,
                Callback>>(
                    method, std::forward<Callback>(callback));

        return *this;
      }

      // Bidi streaming
      template <
          typename Transform = detail::RxGrpcIdentityTransform,
          typename InnerService,
          typename ResponseType,
          typename RequestType,
          typename Callback>
      ServiceBuilder &registerMethod(
          detail::StreamingRequestMethod<
              InnerService,
              grpc::ServerAsyncReaderWriter<ResponseType, RequestType>> method,
          Callback &&callback) {
        registerMethodImpl<
            detail::ServerCallTraits<
                grpc::ServerAsyncReaderWriter<ResponseType, RequestType>,
                Service,
                ResponseType,
                RequestType,
                Transform,
                Callback>>(
                    method, std::forward<Callback>(callback));

        return *this;
      }

     private:
      template <
          typename ServerCallTraits,
          typename Method,
          typename Callback>
      void registerMethodImpl(
          Method method,
          Callback &&callback) {
        using ServerInvocationRequester =
            detail::RxGrpcServerInvocationRequester<
                Method,
                ServerCallTraits,
                Callback>;

        _invocation_requesters.emplace_back(
            new ServerInvocationRequester(
                method, std::move(callback), &_service));
      }

      Service &_service;
      std::vector<std::unique_ptr<detail::InvocationRequester>> &
          _invocation_requesters;
    };

    template <typename Service>
    ServiceBuilder<Service> registerService() {
      auto service = new Service();
      _services.emplace_back(service, [](void *service) {
        delete reinterpret_cast<Service *>(service);
      });
      _builder.RegisterService(service);
      return ServiceBuilder<Service>(service, &_invocation_requesters);
    }

    grpc::ServerBuilder &grpcServerBuilder() {
      return _builder;
    }

    /**
     * Build and start the gRPC server. After calling this method this object is
     * dead and the only valid operation on it is to destroy it.
     */
    RxGrpcServer buildAndStart() {
      RxGrpcServer server(
          std::move(_services),
          _builder.AddCompletionQueue(),
          _builder.BuildAndStart());

      for (const auto &requester: _invocation_requesters) {
        requester->requestInvocation(_error_handler, server._cq.get());
      }

      return server;
    }

   private:
    GrpcErrorHandler _error_handler = [](std::exception_ptr error) {
      std::rethrow_exception(error);
    };
    Services _services;
    std::vector<std::unique_ptr<detail::InvocationRequester>>
        _invocation_requesters;
    grpc::ServerBuilder _builder;
  };

  template <
      typename Transform = detail::RxGrpcIdentityTransform,
      typename Stub>
  RxGrpcServiceClient<Stub, Transform> makeClient(
      std::unique_ptr<Stub> &&stub) {
    return RxGrpcServiceClient<Stub, Transform>(std::move(stub), _cq.get());
  }

  /**
   * Block and process asynchronous events until the server is shut down.
   */
  void run() {
    return detail::RxGrpcTag::processAllEvents(_cq.get());
  }

  /**
   * Block and process one asynchronous event.
   *
   * Returns false if the event queue is shutting down.
   */
  bool next() {
    return detail::RxGrpcTag::processOneEvent(_cq.get());
  }

  void shutdown() {
    // _server and _cq might be nullptr if this object has been moved out from.
    if (_server) {
      _server->Shutdown();
    }
    if (_cq) {
      _cq->Shutdown();
    }
  }

 private:
  // This object doesn't really do anything with the services other than owning
  // them, so that they are valid while the server is servicing requests and
  // that they can be destroyed at the right time.
  Services _services;
  std::unique_ptr<grpc::ServerCompletionQueue> _cq;
  std::unique_ptr<grpc::Server> _server;
};

class RxGrpcClient {
 public:
  ~RxGrpcClient() {
    shutdown();
  }

  template <
      typename Transform = detail::RxGrpcIdentityTransform,
      typename Stub>
  RxGrpcServiceClient<Stub, Transform> makeClient(
      std::unique_ptr<Stub> &&stub) {
    return RxGrpcServiceClient<Stub, Transform>(std::move(stub), &_cq);
  }

  /**
   * Block and process asynchronous events until the server is shut down.
   */
  void run() {
    return detail::RxGrpcTag::processAllEvents(&_cq);
  }

  /**
   * Block and process one asynchronous event.
   *
   * Returns false if the event queue is shutting down.
   */
  bool next() {
    return detail::RxGrpcTag::processOneEvent(&_cq);
  }

  void shutdown() {
    _cq.Shutdown();
  }

 private:
  grpc::CompletionQueue _cq;
};

}  // namespace shk
