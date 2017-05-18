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

#include <grpc++/grpc++.h>
#include <rxcpp/rx-observable.hpp>

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
  static T wrap(T &&value) {
    return std::forward<T>(value);
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

template <
    typename WrappedRequestType, typename ResponseType, typename Transform>
class RxGrpcClientInvocation : public RxGrpcTag {
 public:
  using TransformedResponseType = decltype(
      Transform::wrap(std::declval<ResponseType>()));

  RxGrpcClientInvocation(
      const WrappedRequestType &request,
      rxcpp::subscriber<TransformedResponseType> &&subscriber)
      : _request(request), _subscriber(std::move(subscriber)) {}

  void operator()(bool success) override {
    if (!success) {
      // Unfortunately, gRPC provides literally no information other than that
      // the operation failed.
      _subscriber.on_error(std::make_exception_ptr(GrpcError(grpc::Status(
          grpc::UNKNOWN, "The async function encountered an error"))));
    } else if (_status.ok()) {
      _subscriber.on_next(Transform::wrap(std::move(_response)));
      _subscriber.on_completed();
    } else {
      _subscriber.on_error(std::make_exception_ptr(GrpcError(_status)));
    }

    delete this;
  }

  WrappedRequestType &request() {
    return _request;
  }

  ResponseType &response() {
    return _response;
  }

  grpc::ClientContext &context() {
    return _context;
  }

  grpc::Status &status() {
    return _status;
  }

 private:
  WrappedRequestType _request;
  ResponseType _response;
  rxcpp::subscriber<TransformedResponseType> _subscriber;
  grpc::ClientContext _context;
  grpc::Status _status;
};

template <typename Service, typename ResponseType, typename RequestType>
using RequestAsyncMethod = void (Service::*)(
    grpc::ServerContext *context,
    RequestType *request,
    grpc::ServerAsyncResponseWriter<ResponseType> *response,
    grpc::CompletionQueue *new_call_cq,
    grpc::ServerCompletionQueue *notification_cq,
    void *tag);

template <
    typename Service,
    typename ResponseType,
    typename RequestType,
    typename Transform,
    typename Callback>
class RxGrpcServerInvocation : public RxGrpcTag {
  using OwnedRequest =
      decltype(Transform::wrap(std::declval<RequestType>()));
  using ResponseObservable =
      decltype(std::declval<Callback>()(std::declval<OwnedRequest>()));
  using StatusAndResponse = typename ResponseObservable::value_type;
  using OwnedResponse = typename StatusAndResponse::second_type;
 public:
  using Method = RequestAsyncMethod<Service, ResponseType, RequestType>;

  static void request(
      GrpcErrorHandler error_handler,
      Method method,
      const Callback &callback,
      Service *service,
      grpc::ServerCompletionQueue *cq) {
    auto invocation = new RxGrpcServerInvocation(
        error_handler, method, callback, service, cq);
    (service->*method)(
        &invocation->_context,
        &invocation->_request,
        &invocation->_responder,
        cq,
        cq,
        invocation);
  }

  void operator()(bool success) override {
    if (!success) {
      auto error_handler = _error_handler;
      // Delete this already now, in case error_handler throws.
      delete this;
      // Unfortunately, gRPC provides literally no information other than that
      // the operation failed.
      error_handler(std::make_exception_ptr(GrpcError(grpc::Status(
          grpc::UNKNOWN, "The async function encountered an error"))));
    }

    switch (_state) {
      case State::WAITING_FOR_REQUEST: {
        // TODO(peck): Static assert on the callbacks return and parameter types

        _state = State::SENT_RESPONSE;
        _callback(Transform::wrap(std::move(_request)))
            .subscribe(
                [this](const std::pair<grpc::Status, OwnedResponse> &response) {
                  _response = response.second;
                  _responder.Finish(
                      Transform::unwrap(_response),
                      response.first,
                      this);
                },
                [this](std::exception_ptr error) {
                  const auto what = exceptionMessage(error);
                  const auto status = grpc::Status(grpc::INTERNAL, what);
                  _responder.FinishWithError(status, this);
                });
        break;
      }
      case State::SENT_RESPONSE: {
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
    SENT_RESPONSE
  };

  RxGrpcServerInvocation(
      GrpcErrorHandler error_handler,
      Method method,
      const Callback &callback,
      Service *service,
      grpc::ServerCompletionQueue *cq)
      : _error_handler(error_handler),
        _method(method),
        _callback(callback),
        _service(*service),
        _cq(*cq),
        _responder(&_context) {}

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
  State _state = State::WAITING_FOR_REQUEST;
  Method _method;
  Callback _callback;
  Service &_service;
  grpc::ServerCompletionQueue &_cq;
  grpc::ServerContext _context;
  RequestType _request;
  OwnedResponse _response;
  grpc::ServerAsyncResponseWriter<ResponseType> _responder;
};

class InvocationRequester {
 public:
  virtual ~InvocationRequester() = default;

  virtual void requestInvocation(
      GrpcErrorHandler error_handler,
      grpc::ServerCompletionQueue *cq) = 0;
};

template <
    typename Service,
    typename ResponseType,
    typename RequestType,
    typename Transform,
    typename Callback>
class RxGrpcServerInvocationRequester : public InvocationRequester {
 public:
  using Method = RequestAsyncMethod<Service, ResponseType, RequestType>;

  RxGrpcServerInvocationRequester(
      Method method, Callback &&callback, Service *service)
      : _method(method), _callback(std::move(callback)), _service(*service) {}

  void requestInvocation(
      GrpcErrorHandler error_handler,
      grpc::ServerCompletionQueue *cq) override {
    using ServerInvocation = RxGrpcServerInvocation<
        Service,
        ResponseType,
        RequestType,
        Transform,
        Callback>;
    ServerInvocation::request(error_handler, _method, _callback, &_service, cq);
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

  template <typename ResponseType, typename WrappedRequestType>
  rxcpp::observable<
      typename detail::RxGrpcClientInvocation<
          WrappedRequestType, ResponseType, Transform>::TransformedResponseType>
  invoke(
      std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>>
      (Stub::*invoke)(
          grpc::ClientContext *context,
          const decltype(std::declval<Transform>()
              .unwrap(std::declval<WrappedRequestType>())) &request,
          grpc::CompletionQueue *cq),
      WrappedRequestType &&request,
      grpc::ClientContext &&context = grpc::ClientContext()) {

    using ClientInvocation =
        detail::RxGrpcClientInvocation<
            WrappedRequestType, ResponseType, Transform>;
    using TransformedResponseType =
        typename ClientInvocation::TransformedResponseType;

    return rxcpp::observable<>::create<TransformedResponseType>([&](
        rxcpp::subscriber<TransformedResponseType> subscriber) {

      auto call = std::unique_ptr<ClientInvocation>(
          new ClientInvocation(
              std::forward<WrappedRequestType>(request),
              std::move(subscriber)));
      auto rpc = ((_stub.get())->*invoke)(
          &call->context(),
          Transform::unwrap(call->request()),
          &_cq);
      rpc->Finish(&call->response(), &call->status(), call.release());
    });
  }

 private:
  std::unique_ptr<Stub> _stub;
  grpc::CompletionQueue &_cq;
};

class RxGrpcServer {
 public:
  RxGrpcServer(
      std::vector<std::unique_ptr<grpc::Service>> &&services,
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
       * The pointers passed to the constructor are not owned by this class;
       * they need to stay alive for as long as this object exists.
       */
      ServiceBuilder(
          Service *service,
          std::vector<std::unique_ptr<detail::InvocationRequester>> *requesters)
          : _service(*service),
            _invocation_requesters(*requesters) {}

      template <typename ResponseType, typename RequestType>
      using Method = detail::RequestAsyncMethod<
          Service, ResponseType, RequestType>;

      template <
          typename Transform = detail::RxGrpcIdentityTransform,
          typename ResponseType,
          typename RequestType,
          typename Callback>
      ServiceBuilder &registerMethod(
          Method<ResponseType, RequestType> method,
          Callback &&callback) {
        using ServerInvocationRequester =
            detail::RxGrpcServerInvocationRequester<
                Service,
                ResponseType,
                RequestType,
                Transform,
                Callback>;

        _invocation_requesters.emplace_back(
            new ServerInvocationRequester(
                method, std::move(callback), &_service));

        return *this;
      }

     private:
      Service &_service;
      std::vector<std::unique_ptr<detail::InvocationRequester>> &
          _invocation_requesters;
    };

    template <typename Service>
    ServiceBuilder<Service> registerService() {
      auto service = new Service();
      _services.emplace_back(service);
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
    std::vector<std::unique_ptr<grpc::Service>> _services;
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
  std::vector<std::unique_ptr<grpc::Service>> _services;
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
