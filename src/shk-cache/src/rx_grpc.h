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
#include "rx_grpc_identity_transform.h"
#include "rx_grpc_tag.h"
#include "rx_grpc_writer.h"
#include "stream_traits.h"

namespace shk {
namespace detail {

template <
    typename Reader,
    typename TransformedRequestType,
    typename Transform>
class RxGrpcClientInvocation;

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

/**
 * Unary client RPC.
 */
template <
    typename ResponseType,
    typename TransformedRequestType,
    typename Transform>
class RxGrpcClientInvocation<
    grpc::ClientAsyncResponseReader<ResponseType>,
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
 */
template <
    typename ResponseType,
    typename TransformedRequestType,
    typename Transform>
class RxGrpcClientInvocation<
    grpc::ClientAsyncWriter<ResponseType>,
    TransformedRequestType,
    Transform> : public RxGrpcTag {
  using TransformedResponseType = typename decltype(
      Transform::wrap(std::declval<ResponseType>()))::first_type;

  /**
   * Because the client message stream and the server response have separate
   * lifetimes (one can end without the other), we handle client streaming by
   * creating two separate objects with separate lifetime.
   *
   * This one handles receiving the response.
   */
  class ResponseHandler : public RxGrpcTag {
   public:
    ResponseHandler(rxcpp::subscriber<TransformedResponseType> &&subscriber)
        : _subscriber(subscriber) {}

    void operator()(bool success) override {
      handleUnaryResponse<Transform>(
          success, grpc::Status::OK, std::move(_response), &_subscriber);
      delete this;
    }

    ResponseType &response() {
      return _response;
    }

   private:
    rxcpp::subscriber<TransformedResponseType> _subscriber;
    ResponseType _response;
  };

 public:
  RxGrpcClientInvocation(
      const rxcpp::observable<TransformedRequestType> &requests,
      rxcpp::subscriber<TransformedResponseType> &&subscriber)
      : _subscriber(std::move(subscriber)) {
    requests.subscribe(
        [this](TransformedRequestType &&request) {
          _enqueued_requests.emplace_back(std::move(request));
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
      // This happens when the runloop is shutting down.
      delete this;
      return;
    }

    if (!_sent_final_response) {
      _operation_in_progress = false;
      runEnqueuedOperation();
    } else {
      delete this;
    }
  }

  template <typename Stub, typename RequestType>
  void invoke(
      std::unique_ptr<grpc::ClientAsyncWriter<ResponseType>>
      (Stub::*invoke)(
          grpc::ClientContext *context,
          ResponseType *response,
          grpc::CompletionQueue *cq,
          void *tag),
      Stub *stub,
      grpc::CompletionQueue *cq) {
    auto response_handler = new ResponseHandler(std::move(_subscriber));
    _stream = (stub->*invoke)(
        &_context, &response_handler.response(), cq, response_handler);
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
    } else if (_enqueued_finish) {
      _enqueued_finish = false;
      _operation_in_progress = true;

      // Must be done before the call to Finish because it's not safe to do
      // anything after that call; gRPC could invoke the callback immediately
      // on another thread, which could delete this.
      _sent_final_response = true;

      _stream->Finish(_enqueued_finish_status, this);
    }
  }

  std::unique_ptr<grpc::ClientAsyncWriter<ResponseType>> _stream;
  grpc::ClientContext _context;
  rxcpp::subscriber<TransformedResponseType> _subscriber;

  bool _sent_final_response = false;
  bool _operation_in_progress = false;

  // Because we don't have backpressure we need an unbounded buffer here :-(
  std::deque<TransformedResponseType> _enqueued_requests;
  bool _enqueued_finish = false;
  grpc::Status _enqueued_finish_status;
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
 * pass around tons and tons of template parameters everywhere, and to have
 * something to do partial template specialization on to handle non-streaming
 * vs uni-streaming vs bidi streaming stuff.
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

 private:
  /**
   * The type of the parameter that the request handler callback takes. If it is
   * a streaming request, it's an observable, otherwise it's an object directly.
   */
  using CallbackParamType = typename std::conditional<
      StreamTraits<Stream>::kRequestStreaming,
      rxcpp::observable<TransformedRequest>,
      TransformedRequest>::type;

  using ResponseObservable =
      decltype(std::declval<Callback>()(std::declval<CallbackParamType>()));

 public:
  using TransformedResponse = typename ResponseObservable::value_type;

  using Method = typename std::conditional<
      StreamTraits<Stream>::kRequestStreaming,
      StreamingRequestMethod<Service, Stream>,
      RequestMethod<Service, RequestType, Stream>>::type;
};

template <
    typename OwnerType,
    typename ServerCallTraits,
    bool StreamingRequest>
class ServerStreamOrResponseReader;

/**
 * Non-streaming version.
 */
template <typename OwnerType, typename ServerCallTraits>
class ServerStreamOrResponseReader<OwnerType, ServerCallTraits, false> :
    public RxGrpcTag {
  using CallbackParameter =
      std::pair<typename ServerCallTraits::TransformedRequest, grpc::Status>;
 public:
  ServerStreamOrResponseReader(
      const std::function<void (CallbackParameter &&)> &got_response,
      OwnerType *owner)
      : _owner(*owner),
        _got_response(got_response) {}

  template <typename Writer>
  void request(
      typename ServerCallTraits::Service *service,
      typename ServerCallTraits::Method method,
      grpc::ServerContext *context,
      Writer *writer,
      grpc::ServerCompletionQueue *cq) {
    (service->*method)(context, &_request, writer, cq, cq, this);
  }

  void operator()(bool success) override {
    if (!success) {
      // This happens when the server is shutting down.
      delete &_owner;
      return;
    }

    _got_response(
        ServerCallTraits::Transform::wrap(std::move(_request)));
  }

 private:
  OwnerType &_owner;
  std::function<void (CallbackParameter &&)> _got_response;
  typename ServerCallTraits::Request _request;
};

/**
 * Streaming version.
 */
template <typename OwnerType, typename ServerCallTraits>
class ServerStreamOrResponseReader<OwnerType, ServerCallTraits, true> :
    public RxGrpcTag {
  using TransformedRequest = typename ServerCallTraits::TransformedRequest;
  using CallbackParameter =
      std::pair<rxcpp::observable<TransformedRequest>, grpc::Status>;
 public:
  ServerStreamOrResponseReader(
      const std::function<void (CallbackParameter &&)> &got_response,
      OwnerType *owner)
      : _owner(*owner) {
    // TODO(peck): Implement me
    /*got_response(std::make_pair(
        rxcpp::observable<>::create<TransformedRequest>([](
            rxcpp::subscriber<TransformedRequest> subscriber) {

        }),
        grpc::Status::OK));*/
  }

  template <typename Writer>
  void request(
      typename ServerCallTraits::Service *service,
      typename ServerCallTraits::Method method,
      grpc::ServerContext *context,
      Writer *writer,
      grpc::ServerCompletionQueue *cq) {
    (service->*method)(context, writer, cq, cq, this);
  }

  void operator()(bool success) override {
    if (!success) {
      // This happens when the server is shutting down.
      delete &_owner;
      return;
    }
  }

 private:
  OwnerType &_owner;
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
    Callback> {
  using Stream = grpc::ServerAsyncResponseWriter<ResponseType>;
  using Service = typename ServerCallTraits::Service;
  using Transform = typename ServerCallTraits::Transform;
  using TransformedRequest = typename ServerCallTraits::TransformedRequest;
  using TransformedResponse = typename ServerCallTraits::TransformedResponse;
  using Method = typename ServerCallTraits::Method;

 public:
  static void request(
      GrpcErrorHandler error_handler,
      Method method,
      Callback &&callback,
      Service *service,
      grpc::ServerCompletionQueue *cq) {
    auto invocation = new RxGrpcServerInvocation(
        error_handler, method, std::move(callback), service, cq);
    invocation->_reader.request(
        service,
        method,
        &invocation->_context,
        invocation->_writer.get(),
        cq);
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
        _reader(
            [this](auto &&callback_param) {
              processRequest(std::move(callback_param));
            },
            this),
        _writer(this, &_context) {}

  template <typename CallbackParameter>
  void processRequest(CallbackParameter &&callback_param) {
    auto values = callback_param.second.ok() ?
        _callback(std::move(callback_param.first)).as_dynamic() :
        rxcpp::observable<>::error<TransformedResponse>(
            GrpcError(callback_param.second)).as_dynamic();

    // Request the a new request, so that the server is always waiting for
    // one. This is done after the callback (because this steals it) but
    // before the subscribe call because that could tell gRPC to respond,
    // after which it's not safe to do anything with `this` anymore.
    issueNewServerRequest(std::move(_callback));

    _writer.subscribe(std::move(values));
  }

  void issueNewServerRequest(Callback &&callback) {
    // Take callback as an rvalue parameter to make it obvious that we steal it.
    request(
        _error_handler,
        _method,
        std::move(callback),  // Reuse the callback functor, don't copy
        &_service,
        &_cq);
  }

  GrpcErrorHandler _error_handler;
  Method _method;
  Callback _callback;
  Service &_service;
  grpc::ServerCompletionQueue &_cq;
  grpc::ServerContext _context;
  ServerStreamOrResponseReader<
      RxGrpcServerInvocation,
      ServerCallTraits,
      StreamTraits<Stream>::kRequestStreaming> _reader;
  RxGrpcWriter<
      RxGrpcServerInvocation,
      TransformedResponse,
      Transform,
      Stream,
      StreamTraits<Stream>::kResponseStreaming> _writer;
};

/**
 * Server streaming.
 */
template <typename ResponseType, typename ServerCallTraits, typename Callback>
class RxGrpcServerInvocation<
    grpc::ServerAsyncWriter<ResponseType>,
    ServerCallTraits,
    Callback> {
  using Stream = grpc::ServerAsyncWriter<ResponseType>;
  using Service = typename ServerCallTraits::Service;
  using Transform = typename ServerCallTraits::Transform;
  using TransformedRequest = typename ServerCallTraits::TransformedRequest;
  using TransformedResponse = typename ServerCallTraits::TransformedResponse;
  using Method = typename ServerCallTraits::Method;

 public:
  static void request(
      GrpcErrorHandler error_handler,
      Method method,
      Callback &&callback,
      Service *service,
      grpc::ServerCompletionQueue *cq) {
    auto invocation = new RxGrpcServerInvocation(
        error_handler, method, std::move(callback), service, cq);
    invocation->_reader.request(
        service,
        method,
        &invocation->_context,
        invocation->_writer.get(),
        cq);
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
        _reader(
            [this](auto &&callback_param) {
              processRequest(std::move(callback_param));
            },
            this),
        _writer(this, &_context) {}

  template <typename CallbackParameter>
  void processRequest(CallbackParameter &&callback_param) {
    auto values = callback_param.second.ok() ?
        _callback(std::move(callback_param.first)).as_dynamic() :
        rxcpp::observable<>::error<TransformedResponse>(
            GrpcError(callback_param.second)).as_dynamic();

    // Request the a new request, so that the server is always waiting for
    // one. This is done after the callback (because this steals it) but
    // before the subscribe call because that could tell gRPC to respond,
    // after which it's not safe to do anything with `this` anymore.
    issueNewServerRequest(std::move(_callback));

    _writer.subscribe(std::move(values));
  }

  void issueNewServerRequest(Callback &&callback) {
    // Take callback as an rvalue parameter to make it obvious that we steal it.
    request(
        _error_handler,
        _method,
        std::move(callback),  // Reuse the callback functor, don't copy
        &_service,
        &_cq);
  }

  GrpcErrorHandler _error_handler;
  Method _method;
  Callback _callback;
  Service &_service;
  grpc::ServerCompletionQueue &_cq;
  grpc::ServerContext _context;
  ServerStreamOrResponseReader<
      RxGrpcServerInvocation,
      ServerCallTraits,
      StreamTraits<Stream>::kRequestStreaming> _reader;
  RxGrpcWriter<
      RxGrpcServerInvocation,
      TransformedResponse,
      Transform,
      Stream,
      StreamTraits<Stream>::kResponseStreaming> _writer;
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
    Callback> {
  using Stream = grpc::ServerAsyncReader<ResponseType, RequestType>;
  using Service = typename ServerCallTraits::Service;
  using Transform = typename ServerCallTraits::Transform;
  using TransformedRequest = typename ServerCallTraits::TransformedRequest;
  using TransformedResponse = typename ServerCallTraits::TransformedResponse;
  using Method = typename ServerCallTraits::Method;

 public:
  static void request(
      GrpcErrorHandler error_handler,
      Method method,
      Callback &&callback,
      Service *service,
      grpc::ServerCompletionQueue *cq) {
    auto invocation = new RxGrpcServerInvocation(
        error_handler, method, std::move(callback), service, cq);
    invocation->_reader.request(
        service,
        method,
        &invocation->_context,
        invocation->_writer.get(),
        cq);
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
        _reader(
            [this](auto &&callback_param) {
              processRequest(std::move(callback_param));
            },
            this),
        _writer(this, &_context) {}

  template <typename CallbackParameter>
  void processRequest(CallbackParameter &&callback_param) {
    auto values = callback_param.second.ok() ?
        _callback(std::move(callback_param.first)).as_dynamic() :
        rxcpp::observable<>::error<TransformedResponse>(
            GrpcError(callback_param.second)).as_dynamic();

    // Request the a new request, so that the server is always waiting for
    // one. This is done after the callback (because this steals it) but
    // before the subscribe call because that could tell gRPC to respond,
    // after which it's not safe to do anything with `this` anymore.
    issueNewServerRequest(std::move(_callback));

    _writer.subscribe(std::move(values));
  }

  void issueNewServerRequest(Callback &&callback) {
    // Take callback as an rvalue parameter to make it obvious that we steal it.
    request(
        _error_handler,
        _method,
        std::move(callback),  // Reuse the callback functor, don't copy
        &_service,
        &_cq);
  }

  GrpcErrorHandler _error_handler;
  Method _method;
  Callback _callback;
  Service &_service;
  grpc::ServerCompletionQueue &_cq;
  grpc::ServerContext _context;
  ServerStreamOrResponseReader<
      RxGrpcServerInvocation,
      ServerCallTraits,
      StreamTraits<Stream>::kRequestStreaming> _reader;
  RxGrpcWriter<
      RxGrpcServerInvocation,
      TransformedResponse,
      Transform,
      Stream,
      StreamTraits<Stream>::kResponseStreaming> _writer;
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
   * Non-stream response.
   */
  template <typename ResponseType, typename TransformedRequestType>
  rxcpp::observable<
      typename detail::RxGrpcClientInvocation<
          grpc::ClientAsyncResponseReader<ResponseType>,
          TransformedRequestType,
          Transform>::TransformedResponseType>
  invoke(
      std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>>
      (Stub::*invoke)(
          grpc::ClientContext *context,
          const decltype(std::declval<Transform>()
              .unwrap(std::declval<TransformedRequestType>())) &request,
          grpc::CompletionQueue *cq),
      const TransformedRequestType &request,
      grpc::ClientContext &&context = grpc::ClientContext()) {
    return invokeImpl<
        grpc::ClientAsyncResponseReader<ResponseType>,
        ResponseType>(
            invoke,
            request,
            std::move(context));
  }

  /**
   * Stream response.
   */
  template <typename ResponseType, typename TransformedRequestType>
  rxcpp::observable<
      typename detail::RxGrpcClientInvocation<
          grpc::ClientAsyncReader<ResponseType>,
          TransformedRequestType,
          Transform>::TransformedResponseType>
  invoke(
      std::unique_ptr<grpc::ClientAsyncReader<ResponseType>>
      (Stub::*invoke)(
          grpc::ClientContext *context,
          const decltype(std::declval<Transform>()
              .unwrap(std::declval<TransformedRequestType>())) &request,
          grpc::CompletionQueue *cq,
          void *tag),
      const TransformedRequestType &request,
      grpc::ClientContext &&context = grpc::ClientContext()) {
    return invokeImpl<
        grpc::ClientAsyncReader<ResponseType>,
        ResponseType>(
            invoke,
            request,
            std::move(context));
  }

  /**
   * Stream request.
   */
  template <
      typename RequestType,
      typename ResponseType,
      typename TransformedRequestType>
  rxcpp::observable<
      typename detail::RxGrpcClientInvocation<
          grpc::ClientAsyncWriter<RequestType>,
          TransformedRequestType,
          Transform>::TransformedResponseType>
  invoke(
      std::unique_ptr<grpc::ClientAsyncWriter<RequestType>>
      (Stub::*invoke)(
          grpc::ClientContext *context,
          ResponseType *response,
          grpc::CompletionQueue *cq,
          void *tag),
      const rxcpp::observable<TransformedRequestType> &requests,
      grpc::ClientContext &&context = grpc::ClientContext()) {
    return invokeImpl<
        grpc::ClientAsyncWriter<RequestType>,
        ResponseType>(
            invoke,
            requests,
            std::move(context));
  }

 private:
  template <
      typename Reader,
      typename ResponseType,
      typename Invoke,
      typename TransformedRequestType>
  rxcpp::observable<
      typename detail::RxGrpcClientInvocation<
          Reader,
          TransformedRequestType,
          Transform>::TransformedResponseType>
  invokeImpl(
      Invoke invoke,
      const TransformedRequestType &request,
      grpc::ClientContext &&context = grpc::ClientContext()) {

    using ClientInvocation =
        detail::RxGrpcClientInvocation<
            Reader, TransformedRequestType, Transform>;
    using TransformedResponseType =
        typename ClientInvocation::TransformedResponseType;

    return rxcpp::observable<>::create<TransformedResponseType>([
        this, request, invoke](
            rxcpp::subscriber<TransformedResponseType> subscriber) {

      auto call = new ClientInvocation(
          request, std::move(subscriber));
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

      // Non-streaming request, non-streaming response
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

      // Non-streaming request streaming response
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

      // Streaming request, non-streaming response
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
