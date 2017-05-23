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

namespace shk {

using GrpcErrorHandler = std::function<void (std::exception_ptr)>;

class GrpcError : public std::runtime_error {
 public:
  explicit GrpcError(const grpc::Status &status)
      : runtime_error(what(status)),
        _status(status) {}

  const char *what() const throw() override {
    return what(_status);
  }

 private:
  static const char *what(const grpc::Status &status) throw() {
    const auto &message = status.error_message();
    return message.empty() ? "[No error message]" : message.c_str();
  }

  const grpc::Status _status;
};

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

class RxGrpcTag {
 public:
  virtual ~RxGrpcTag() = default;

  virtual void operator()(bool success) = 0;

  /**
   * Block and process one asynchronous event on the given CompletionQueue.
   *
   * Returns false if the event queue is shutting down.
   */
  static bool processOneEvent(grpc::CompletionQueue *cq) {
    void *got_tag;
    bool success = false;
    if (!cq->Next(&got_tag, &success)) {
      // Shutting down
      return false;
    }

    detail::RxGrpcTag *tag = reinterpret_cast<detail::RxGrpcTag *>(got_tag);
    (*tag)(success);

    return true;
  }

  static void processAllEvents(grpc::CompletionQueue *cq) {
    while (processOneEvent(cq)) {}
  }
};

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

template <
    typename Reader,
    typename WrappedRequestType,
    typename ResponseType,
    typename Transform>
class RxGrpcClientInvocation;

/**
 * Non-streaming version
 */
template <
    typename WrappedRequestType,
    typename ResponseType,
    typename Transform>
class RxGrpcClientInvocation<
    grpc::ClientAsyncResponseReader<ResponseType>,
    WrappedRequestType,
    ResponseType,
    Transform> : public RxGrpcTag {
 public:
  using WrappedResponseType = typename decltype(
      Transform::wrap(std::declval<ResponseType>()))::first_type;

  RxGrpcClientInvocation(
      const WrappedRequestType &request,
      rxcpp::subscriber<WrappedResponseType> &&subscriber)
      : _request(request), _subscriber(std::move(subscriber)) {}

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

    delete this;
  }

  template <typename Stub>
  void invoke(
      std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>>
      (Stub::*invoke)(
          grpc::ClientContext *context,
          const decltype(std::declval<Transform>()
              .unwrap(std::declval<WrappedRequestType>())) &request,
          grpc::CompletionQueue *cq),
      Stub *stub,
      grpc::CompletionQueue *cq) {
    auto rpc = (stub->*invoke)(&_context, Transform::unwrap(_request), cq);
    rpc->Finish(&_response, &_status, this);
  }

 private:
  static_assert(
      !std::is_reference<WrappedRequestType>::value,
      "Request type must be held by value");
  WrappedRequestType _request;
  ResponseType _response;
  rxcpp::subscriber<WrappedResponseType> _subscriber;
  grpc::ClientContext _context;
  grpc::Status _status;
};

/**
 * Streaming version
 */
template <
    typename WrappedRequestType,
    typename ResponseType,
    typename Transform>
class RxGrpcClientInvocation<
    grpc::ClientAsyncReader<ResponseType>,
    WrappedRequestType,
    ResponseType,
    Transform> : public RxGrpcTag {
 public:
  using WrappedResponseType = typename decltype(
      Transform::wrap(std::declval<ResponseType>()))::first_type;

  RxGrpcClientInvocation(
      const WrappedRequestType &request,
      rxcpp::subscriber<WrappedResponseType> &&subscriber)
      : _request(request), _subscriber(std::move(subscriber)) {}

  void operator()(bool success) override {
    switch (_state) {
      case State::INIT: {
        _state = State::READING_RESPONSE;
        _reader->Read(&_response, this);
        break;
      }
      case State::READING_RESPONSE: {
        if (!success) {
          // We have reached the end of the stream.
          _state = State::FINISHING;
          _reader->Finish(&_status, this);
        } else {
          auto wrapped = Transform::wrap(std::move(_response));
          if (wrapped.second.ok()) {
            _subscriber.on_next(std::move(wrapped.first));
            _reader->Read(&_response, this);
          } else {
            _subscriber.on_error(
                std::make_exception_ptr(GrpcError(wrapped.second)));
            _state = State::READ_FAILURE;
            _context.TryCancel();
            _reader->Finish(&_status, this);
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

  template <typename Stub>
  void invoke(
      std::unique_ptr<grpc::ClientAsyncReader<ResponseType>>
      (Stub::*invoke)(
          grpc::ClientContext *context,
          const decltype(std::declval<Transform>()
              .unwrap(std::declval<WrappedRequestType>())) &request,
          grpc::CompletionQueue *cq,
          void *tag),
      Stub *stub,
      grpc::CompletionQueue *cq) {
    _reader = (stub->*invoke)(
        &_context, Transform::unwrap(_request), cq, this);
  }

 private:
  enum class State {
    INIT,
    READING_RESPONSE,
    FINISHING,
    READ_FAILURE
  };

  State _state = State::INIT;
  static_assert(
      !std::is_reference<WrappedRequestType>::value,
      "Request type must be held by value");
  WrappedRequestType _request;
  ResponseType _response;
  rxcpp::subscriber<WrappedResponseType> _subscriber;
  grpc::ClientContext _context;
  grpc::Status _status;
  std::unique_ptr<grpc::ClientAsyncReader<ResponseType>> _reader;
};

/**
 * For requests with a non-streaming response.
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
 * For requests with a streaming response.
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

template <typename ServerCallTraits, bool StreamingRequest>
class ServerStreamOrResponseReader;

/**
 * Non-streaming version.
 */
template <typename ServerCallTraits>
class ServerStreamOrResponseReader<ServerCallTraits, false> {
 public:
  template <typename Writer>
  void request(
      typename ServerCallTraits::Service *service,
      typename ServerCallTraits::Method method,
      grpc::ServerContext *context,
      Writer *writer,
      grpc::ServerCompletionQueue *cq,
      void *tag) {
    (service->*method)(context, &_request, writer, cq, cq, tag);
  }

  std::pair<typename ServerCallTraits::TransformedRequest, grpc::Status>
  getCallbackParameter() {
    return ServerCallTraits::Transform::wrap(std::move(_request));
  }

 private:
  typename ServerCallTraits::Request _request;
};

/**
 * Streaming version.
 */
template <typename ServerCallTraits>
class ServerStreamOrResponseReader<ServerCallTraits, true> {
  using TransformedRequest = typename ServerCallTraits::TransformedRequest;
 public:
  template <typename Writer>
  void request(
      typename ServerCallTraits::Service *service,
      typename ServerCallTraits::Method method,
      grpc::ServerContext *context,
      Writer *writer,
      grpc::ServerCompletionQueue *cq,
      void *tag) {
    (service->*method)(context, writer, cq, cq, tag);
  }

  std::pair<rxcpp::observable<TransformedRequest>, grpc::Status>
  getCallbackParameter() {
    return std::make_pair(
        rxcpp::observable<>::create<
            TransformedRequest>([](
                rxcpp::subscriber<TransformedRequest> subcription) {
            }),
        grpc::Status::OK);
  }
};

/**
 * Helper class that exposes a unified interface for either stream or non-stream
 * server response writers.
 */
template <
    typename TransformedResponse,
    typename Transform,
    typename Stream,
    bool StreamingResponse>
class ServerStreamOrResponseWriter;

/**
 * Non-streaming version.
 */
template <
    typename TransformedResponse,
    typename Transform,
    typename Stream>
class ServerStreamOrResponseWriter<
    TransformedResponse, Transform, Stream, false> {
 public:
  ServerStreamOrResponseWriter(grpc::ServerContext *context)
      : _stream(context) {}

  Stream *get() {
    return &_stream;
  }

  void write(TransformedResponse &&response, void *tag) {
    _response = std::move(response);
  }

  template <typename WillDoFinalWrite>
  void finish(void *tag, const WillDoFinalWrite &will_do_final_write) {
    will_do_final_write();
    _stream.Finish(Transform::unwrap(_response), grpc::Status::OK, tag);
  }

  template <typename WillDoFinalWrite>
  void finishWithError(
      const grpc::Status &status,
      void *tag,
      const WillDoFinalWrite &will_do_final_write) {
    will_do_final_write();
    _stream.FinishWithError(status, tag);
  }

  template <typename WillDoFinalWrite>
  void operationFinished(
      void *tag, const WillDoFinalWrite &will_do_final_write) {
  }

 private:
  TransformedResponse _response;
  Stream _stream;
};

/**
 * Streaming version.
 */
template <
    typename TransformedResponse,
    typename Transform,
    typename Stream>
class ServerStreamOrResponseWriter<
    TransformedResponse, Transform, Stream, true> {
 public:
  ServerStreamOrResponseWriter(grpc::ServerContext *context)
      : _stream(context) {}

  Stream *get() {
    return &_stream;
  }

  void write(const TransformedResponse &response, void *tag) {
    _enqueued_responses.push_back(response);
    runEnqueuedOperation(tag, [] { abort(); /* can't happen */ });
  }

  template <typename WillDoFinalWrite>
  void finish(void *tag, const WillDoFinalWrite &will_do_final_write) {
    _enqueued_finish_status = grpc::Status::OK;
    _enqueued_finish = true;
    runEnqueuedOperation(tag, will_do_final_write);
  }

  template <typename WillDoFinalWrite>
  void finishWithError(
      const grpc::Status &status,
      void *tag,
      const WillDoFinalWrite &will_do_final_write) {
    _enqueued_finish_status = status;
    _enqueued_finish = true;
    runEnqueuedOperation(tag, will_do_final_write);
  }

  template <typename WillDoFinalWrite>
  void operationFinished(
      void *tag, const WillDoFinalWrite &will_do_final_write) {
    _operation_in_progress = false;
    runEnqueuedOperation(tag, will_do_final_write);
  }

 private:
  template <typename WillDoFinalWrite>
  void runEnqueuedOperation(
      void *tag, const WillDoFinalWrite &will_do_final_write) {
    if (_operation_in_progress) {
      return;
    }
    if (!_enqueued_responses.empty()) {
      _operation_in_progress = true;
      _stream.Write(
          Transform::unwrap(std::move(_enqueued_responses.front())), tag);
      _enqueued_responses.pop_front();
    } else if (_enqueued_finish) {
      _enqueued_finish = false;
      _operation_in_progress = true;

      // Must be called before the call to Finish because it's not safe to do
      // anything after that call; gRPC could invoke the callback immediately
      // on another thread, which could delete tag.
      will_do_final_write();

      _stream.Finish(_enqueued_finish_status, tag);
    }
  }

  bool _operation_in_progress = false;

  // Because we don't have backpressure we need an unbounded buffer here :-(
  std::deque<TransformedResponse> _enqueued_responses;
  bool _enqueued_finish = false;
  grpc::Status _enqueued_finish_status;

  Stream _stream;
};

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

template <typename ServerCallTraits, typename Callback>
class RxGrpcServerInvocation : public RxGrpcTag {
  using Stream = typename ServerCallTraits::Stream;
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
        cq,
        invocation);
  }

  void operator()(bool success) override {
    if (!success) {
      // This happens when the server is shutting down.
      delete this;
      return;
    }

    switch (_state) {
      case State::WAITING_FOR_REQUEST: {
        _state = State::GOT_REQUEST;

        auto callback_param = _reader.getCallbackParameter();
        if (callback_param.second.ok()) {
          auto values = _callback(std::move(callback_param.first));

          // Request the a new request, so that the server is always waiting for
          // one. This is done after the callback (because this steals it) but
          // before the subscribe call because that could tell gRPC to respond,
          // after which it's not safe to do anything with `this` anymore.
          issueNewServerRequest(std::move(_callback));

          values.subscribe(
              [this](TransformedResponse response) {
                _writer.write(std::move(response), this);
              },
              [this](std::exception_ptr error) {
                // TODO(peck): Make it possible to respond with other errors
                // than INTERNAL (by catching GrpcErrors and reporting that)
                const auto what = exceptionMessage(error);
                const auto status = grpc::Status(grpc::INTERNAL, what);
                _writer.finishWithError(status, this, [this] {
                  _state = State::SENT_FINAL_RESPONSE;
                });
              },
              [this]() {
                _writer.finish(this, [this] {
                  _state = State::SENT_FINAL_RESPONSE;
                });
              });
        } else {
          _writer.finishWithError(callback_param.second, this, [this] {
            _state = State::SENT_FINAL_RESPONSE;
          });
        }
        break;
      }
      case State::GOT_REQUEST: {
        _writer.operationFinished(this, [this] {
          _state = State::SENT_FINAL_RESPONSE;
        });
        break;
      }
      case State::SENT_FINAL_RESPONSE: {
        delete this;
        break;
      }
      default: {
        // Should be unreachable code
        abort();
      }
    }
  }

 private:
  enum class State {
    WAITING_FOR_REQUEST,
    GOT_REQUEST,
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
        _writer(&_context) {}

  void issueNewServerRequest(Callback &&callback) {
    // Take callback as an rvalue parameter to make it obvious that we steal it.
    request(
        _error_handler,
        _method,
        std::move(callback),  // Reuse the callback functor, don't copy
        &_service,
        &_cq);
  }

  static std::string exceptionMessage(const std::exception_ptr &error) {
    try {
      std::rethrow_exception(error);
    } catch (const std::exception &exception) {
      return exception.what();
    } catch (...) {
      return "Unknown error";
    }
  }

  GrpcErrorHandler _error_handler;
  // TODO(peck): Does _state need to be atomic?
  State _state = State::WAITING_FOR_REQUEST;
  Method _method;
  Callback _callback;
  Service &_service;
  grpc::ServerCompletionQueue &_cq;
  grpc::ServerContext _context;
  ServerStreamOrResponseReader<
      ServerCallTraits,
      StreamTraits<Stream>::kRequestStreaming> _reader;
  ServerStreamOrResponseWriter<
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
  template <typename ResponseType, typename WrappedRequestType>
  rxcpp::observable<
      typename detail::RxGrpcClientInvocation<
          grpc::ClientAsyncResponseReader<ResponseType>,
          WrappedRequestType,
          ResponseType,
          Transform>::WrappedResponseType>
  invoke(
      std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>>
      (Stub::*invoke)(
          grpc::ClientContext *context,
          const decltype(std::declval<Transform>()
              .unwrap(std::declval<WrappedRequestType>())) &request,
          grpc::CompletionQueue *cq),
      const WrappedRequestType &request,
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
  template <typename ResponseType, typename WrappedRequestType>
  rxcpp::observable<
      typename detail::RxGrpcClientInvocation<
          grpc::ClientAsyncReader<ResponseType>,
          WrappedRequestType,
          ResponseType,
          Transform>::WrappedResponseType>
  invoke(
      std::unique_ptr<grpc::ClientAsyncReader<ResponseType>>
      (Stub::*invoke)(
          grpc::ClientContext *context,
          const decltype(std::declval<Transform>()
              .unwrap(std::declval<WrappedRequestType>())) &request,
          grpc::CompletionQueue *cq,
          void *tag),
      const WrappedRequestType &request,
      grpc::ClientContext &&context = grpc::ClientContext()) {
    return invokeImpl<
        grpc::ClientAsyncReader<ResponseType>,
        ResponseType>(
            invoke,
            request,
            std::move(context));
  }

 private:
  template <
      typename Reader,
      typename ResponseType,
      typename Invoke,
      typename WrappedRequestType>
  rxcpp::observable<
      typename detail::RxGrpcClientInvocation<
          Reader,
          WrappedRequestType,
          ResponseType,
          Transform>::WrappedResponseType>
  invokeImpl(
      Invoke invoke,
      const WrappedRequestType &request,
      grpc::ClientContext &&context = grpc::ClientContext()) {

    using ClientInvocation =
        detail::RxGrpcClientInvocation<
            Reader, WrappedRequestType, ResponseType, Transform>;
    using WrappedResponseType =
        typename ClientInvocation::WrappedResponseType;

    return rxcpp::observable<>::create<WrappedResponseType>([
        this, request, invoke](
            rxcpp::subscriber<WrappedResponseType> subscriber) {

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
