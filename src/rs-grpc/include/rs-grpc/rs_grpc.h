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

#include <rs/publisher.h>
#include <rs/subscriber.h>
#include <rs/subscription.h>
#include <rs/throw.h>
#include <rs-grpc/grpc_error.h>
#include <rs-grpc/rs_grpc_identity_transform.h>
#include <rs-grpc/rs_grpc_tag.h>

namespace shk {
namespace detail {

template <
    typename Transform,
    typename ResponseType>
void HandleUnaryResponse(
    bool success,
    const grpc::Status &status,
    ResponseType &&response,
    // TODO(peck): Consider avoiding type erasure here
    Subscriber<typename decltype(Transform::wrap(
        std::declval<ResponseType>()))::first_type> *subscriber) {
  // TODO(peck): What about backpressure?
  if (!success) {
    subscriber->OnError(std::make_exception_ptr(GrpcError(grpc::Status(
        grpc::UNKNOWN, "The request was interrupted"))));
  } else if (status.ok()) {
    auto wrapped = Transform::wrap(std::move(response));
    if (wrapped.second.ok()) {
      subscriber->OnNext(std::move(wrapped.first));
      subscriber->OnComplete();
    } else {
      subscriber->OnError(
          std::make_exception_ptr(GrpcError(wrapped.second)));
    }
  } else {
    subscriber->OnError(std::make_exception_ptr(GrpcError(status)));
  }
}

template <
    typename Reader,
    typename ResponseType,
    typename TransformedRequestType,
    typename Transform>
class RsGrpcClientInvocation;

/**
 * Unary client RPC.
 */
template <
    typename ResponseType,
    typename TransformedRequestType,
    typename Transform>
class RsGrpcClientInvocation<
    grpc::ClientAsyncResponseReader<ResponseType>,
    ResponseType,
    TransformedRequestType,
    Transform> : public RsGrpcTag {
 public:
  using TransformedResponseType = typename decltype(
      Transform::wrap(std::declval<ResponseType>()))::first_type;

  RsGrpcClientInvocation(
      const TransformedRequestType &request,
      Subscriber<TransformedResponseType> &&subscriber)
      : request_(request),
        subscriber_(std::move(subscriber)) {}

  void operator()(bool success) override {
    HandleUnaryResponse<Transform>(
        success, status_, std::move(response_), &subscriber_);
    delete this;
  }

  template <typename Stub, typename RequestType>
  auto Invoke(
      std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>>
      (Stub::*invoke)(
          grpc::ClientContext *context,
          const RequestType &request,
          grpc::CompletionQueue *cq),
      Stub *stub,
      grpc::CompletionQueue *cq) {
    auto stream = (stub->*invoke)(&context_, Transform::unwrap(request_), cq);
    stream->Finish(&response_, &status_, this);

    return MakeSubscription();  // TODO(peck): Handle backrpressure and cancellation
  }

 private:
  static_assert(
      !std::is_reference<TransformedRequestType>::value,
      "Request type must be held by value");
  TransformedRequestType request_;
  grpc::ClientContext context_;
  ResponseType response_;
  // TODO(peck): Consider avoiding type erasure here
  Subscriber<TransformedResponseType> subscriber_;
  grpc::Status status_;
};

/**
 * Server streaming.
 */
template <
    typename ResponseType,
    typename TransformedRequestType,
    typename Transform>
class RsGrpcClientInvocation<
    grpc::ClientAsyncReader<ResponseType>,
    ResponseType,
    TransformedRequestType,
    Transform> : public RsGrpcTag {
 public:
  using TransformedResponseType = typename decltype(
      Transform::wrap(std::declval<ResponseType>()))::first_type;

  RsGrpcClientInvocation(
      const TransformedRequestType &request,
      Subscriber<TransformedResponseType> &&subscriber)
      : request_(request),
        subscriber_(std::move(subscriber)) {}

  void operator()(bool success) override {
    switch (state_) {
      case State::INIT: {
        state_ = State::READING_RESPONSE;
        stream_->Read(&response_, this);
        break;
      }
      case State::READING_RESPONSE: {
        if (!success) {
          // We have reached the end of the stream.
          state_ = State::FINISHING;
          stream_->Finish(&status_, this);
        } else {
          auto wrapped = Transform::wrap(std::move(response_));
          if (wrapped.second.ok()) {
            subscriber_.OnNext(std::move(wrapped.first));
            stream_->Read(&response_, this);
          } else {
            subscriber_.OnError(
                std::make_exception_ptr(GrpcError(wrapped.second)));
            state_ = State::READ_FAILURE;
            context_.TryCancel();
            stream_->Finish(&status_, this);
          }
        }
        break;
      }
      case State::FINISHING: {
        if (status_.ok()) {
          subscriber_.OnComplete();
        } else {
          subscriber_.OnError(std::make_exception_ptr(GrpcError(status_)));
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
  auto Invoke(
      std::unique_ptr<grpc::ClientAsyncReader<ResponseType>>
      (Stub::*invoke)(
          grpc::ClientContext *context,
          const RequestType &request,
          grpc::CompletionQueue *cq,
          void *tag),
      Stub *stub,
      grpc::CompletionQueue *cq) {
    stream_ = (stub->*invoke)(&context_, Transform::unwrap(request_), cq, this);

    return MakeSubscription();  // TODO(peck): Handle backrpressure and cancellation
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
  TransformedRequestType request_;
  grpc::ClientContext context_;

  State state_ = State::INIT;
  ResponseType response_;
  // TODO(peck): Consider avoiding type erasure here
  Subscriber<TransformedResponseType> subscriber_;
  grpc::Status status_;
  std::unique_ptr<grpc::ClientAsyncReader<ResponseType>> stream_;
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
class RsGrpcClientInvocation<
    grpc::ClientAsyncWriter<RequestType>,
    ResponseType,
    TransformedRequestType,
    Transform> : public RsGrpcTag {
 public:
  using TransformedResponseType = typename decltype(
      Transform::wrap(std::declval<ResponseType>()))::first_type;

 public:
  template <typename Publisher>
  RsGrpcClientInvocation(
      Publisher &&requests,
      Subscriber<TransformedResponseType> &&subscriber)
      : requests_(std::forward<Publisher>(requests)),
        subscriber_(std::move(subscriber)) {
    static_assert(
        IsPublisher<Publisher>,
        "First parameter must be a Publisher");
  }

  void operator()(bool success) override {
    if (sent_final_request_) {
      if (request_stream_error_) {
        subscriber_.on_error(request_stream_error_);
      } else {
        HandleUnaryResponse<Transform>(
            success, status_, std::move(response_), &subscriber_);
      }
      delete this;
    } else {
      if (success) {
        operation_in_progress_ = false;
        RunEnqueuedOperation();
      } else {
        // This happens when the runloop is shutting down.
        HandleUnaryResponse<Transform>(
            success, status_, std::move(response_), &subscriber_);
        delete this;
      }
    }
  }

  template <typename Stub>
  auto Invoke(
      std::unique_ptr<grpc::ClientAsyncWriter<RequestType>>
      (Stub::*invoke)(
          grpc::ClientContext *context,
          ResponseType *response,
          grpc::CompletionQueue *cq,
          void *tag),
      Stub *stub,
      grpc::CompletionQueue *cq) {
    stream_ = (stub->*invoke)(&context_, &response_, cq, this);
    operation_in_progress_ = true;

    // TODO(peck): Don't hold weak unsafe refs to this
    auto subscription = requests_.Subscribe(MakeSubscriber(
        [this](TransformedRequestType request) {
          enqueued_requests_.emplace_back(std::move(request));
          RunEnqueuedOperation();
        },
        [this](std::exception_ptr &&error) {
          // This triggers RunEnqueuedOperation to Finish the stream.
          request_stream_error_ = error;
          enqueued_writes_done_ = true;
          RunEnqueuedOperation();
        },
        [this]() {
          enqueued_writes_done_ = true;
          RunEnqueuedOperation();
        }));
    // TODO(peck): Backpressure, cancellation
    subscription.Request(ElementCount::Infinite());

    return MakeSubscription();  // TODO(peck): Handle backrpressure and cancellation
  }

 private:
  void RunEnqueuedOperation() {
    if (operation_in_progress_) {
      return;
    }
    if (!enqueued_requests_.empty()) {
      operation_in_progress_ = true;
      stream_->Write(
          Transform::unwrap(std::move(enqueued_requests_.front())), this);
      enqueued_requests_.pop_front();
    } else if (enqueued_writes_done_) {
      enqueued_writes_done_ = false;
      enqueued_finish_ = true;
      operation_in_progress_ = true;
      stream_->WritesDone(this);
    } else if (enqueued_finish_) {
      enqueued_finish_ = false;
      operation_in_progress_ = true;

      // Must be done before the call to Finish because it's not safe to do
      // anything after that call; gRPC could invoke the callback immediately
      // on another thread, which could delete this.
      sent_final_request_ = true;

      stream_->Finish(&status_, this);
    }
  }

  // TODO(peck): Consider avoiding type erasure here
  Publisher<TransformedRequestType> requests_;
  ResponseType response_;
  std::unique_ptr<grpc::ClientAsyncWriter<RequestType>> stream_;
  grpc::ClientContext context_;
  // TODO(peck): Consider avoiding type erasure here
  Subscriber<TransformedResponseType> subscriber_;

  std::exception_ptr request_stream_error_;
  bool sent_final_request_ = false;
  bool operation_in_progress_ = false;

  // Because we don't have backpressure we need an unbounded buffer here :-(
  std::deque<TransformedRequestType> enqueued_requests_;
  bool enqueued_writes_done_ = false;
  bool enqueued_finish_ = false;
  grpc::Status status_;
};

/**
 * Bidi.
 */
template <
    typename RequestType,
    typename ResponseType,
    typename TransformedRequestType,
    typename Transform>
class RsGrpcClientInvocation<
    grpc::ClientAsyncReaderWriter<RequestType, ResponseType>,
    ResponseType,
    TransformedRequestType,
    Transform> : public RsGrpcTag {
 public:
  using TransformedResponseType = typename decltype(
      Transform::wrap(std::declval<ResponseType>()))::first_type;

 private:
  /**
   * Bidi streaming requires separate tags for reading and writing (since they
   * can happen simultaneously). The purpose of this class is to be the other
   * tag. It's used for reading.
   */
  class Reader : public RsGrpcTag {
   public:
    Reader(
        const std::function<void ()> &shutdown,
        Subscriber<TransformedResponseType> &&subscriber)
        : shutdown_(shutdown),
          subscriber_(std::move(subscriber)) {}

    void Invoke(
        grpc::ClientAsyncReaderWriter<RequestType, ResponseType> *stream) {
      stream_ = stream;
      stream_->Read(&response_, this);
    }

    /**
     * Try to signal an error to the subscriber. If subscriber stream has
     * already been closed, this is a no-op.
     */
    void OnError(const std::exception_ptr &error) {
      error_ = error;
    }

    /**
     * Should be called by _shutdown when the stream is actually about to be
     * destroyed. We don't call on_error or on_completed on the subscriber until
     * then because it's not until both the read stream and the write stream
     * have finished that it is known for sure that there was no error.
     */
    void Finish(const grpc::Status &status) {
      if (!status.ok()) {
        subscriber_.on_error(std::make_exception_ptr(GrpcError(status)));
      } else if (error_) {
        subscriber_.on_error(error_);
      } else {
        subscriber_.on_completed();
      }
    }

    void operator()(bool success) override {
      if (!success || error_) {
        // We have reached the end of the stream.
        shutdown_();
      } else {
        auto wrapped = Transform::wrap(std::move(response_));
        if (wrapped.second.ok()) {
          subscriber_.on_next(std::move(wrapped.first));
          stream_->Read(&response_, this);
        } else {
          error_ = std::make_exception_ptr(GrpcError(wrapped.second));
          shutdown_();
        }
      }
    }

   private:
    std::exception_ptr error_;
    // Should be called when the read stream is finished. Care must be taken so
    // that this is not called when there is an outstanding async operation.
    std::function<void ()> shutdown_;
    grpc::ClientAsyncReaderWriter<RequestType, ResponseType> *stream_ = nullptr;
    // TODO(peck): Consider avoiding type erasure here
    Subscriber<TransformedResponseType> subscriber_;
    ResponseType response_;
  };

 public:
  template <typename Publisher>
  RsGrpcClientInvocation(
      const Publisher &requests,
      Subscriber<TransformedResponseType> &&subscriber)
      : reader_(
            [this] { reader_done_ = true; TryShutdown(); },
            std::move(subscriber)),
        requests_(requests.as_dynamic()) {
    static_assert(
        IsPublisher<Publisher>,
        "First parameter must be a Publisher");
  }

  void operator()(bool success) override {
    if (sent_final_request_) {
      writer_done_ = true;
      TryShutdown();
    } else {
      if (success) {
        operation_in_progress_ = false;
        RunEnqueuedOperation();
      } else {
        // This happens when the runloop is shutting down.
        writer_done_ = true;
        TryShutdown();
      }
    }
  }

  template <typename Stub>
  void Invoke(
      std::unique_ptr<grpc::ClientAsyncReaderWriter<RequestType, ResponseType>>
      (Stub::*invoke)(
          grpc::ClientContext *context,
          grpc::CompletionQueue *cq,
          void *tag),
      Stub *stub,
      grpc::CompletionQueue *cq) {
    stream_ = (stub->*invoke)(&context_, cq, this);
    reader_.Invoke(stream_.get());
    operation_in_progress_ = true;

    requests_.subscribe(
        [this](TransformedRequestType request) {
          enqueued_requests_.emplace_back(std::move(request));
          RunEnqueuedOperation();
        },
        [this](std::exception_ptr &&error) {
          reader_.OnError(error);
          enqueued_writes_done_ = true;
          RunEnqueuedOperation();
        },
        [this]() {
          enqueued_writes_done_ = true;
          RunEnqueuedOperation();
        });
  }

 private:
  enum class State {
    SENDING_REQUESTS,
    TO_SEND_WRITES_DONE,
    TO_SEND_FINISH,
    DONE
  };

  void RunEnqueuedOperation() {
    if (operation_in_progress_) {
      return;
    }
    if (!enqueued_requests_.empty()) {
      operation_in_progress_ = true;
      stream_->Write(
          Transform::unwrap(std::move(enqueued_requests_.front())), this);
      enqueued_requests_.pop_front();
    } else if (enqueued_writes_done_) {
      enqueued_writes_done_ = false;
      enqueued_finish_ = true;
      operation_in_progress_ = true;
      stream_->WritesDone(this);
    } else if (enqueued_finish_) {
      enqueued_finish_ = false;
      operation_in_progress_ = true;

      // Must be done before the call to Finish because it's not safe to do
      // anything after that call; gRPC could invoke the callback immediately
      // on another thread, which could delete this.
      sent_final_request_ = true;

      stream_->Finish(&status_, this);
    }
  }

  void TryShutdown() {
    if (writer_done_ && reader_done_) {
      reader_.Finish(status_);
      delete this;
    }
  }

  Reader reader_;
  bool reader_done_ = false;

  // TODO(peck): Consider avoiding type erasure here
  Publisher<TransformedRequestType> requests_;
  ResponseType response_;
  std::unique_ptr<
      grpc::ClientAsyncReaderWriter<RequestType, ResponseType>> stream_;
  grpc::ClientContext context_;

  bool sent_final_request_ = false;
  bool operation_in_progress_ = false;
  bool writer_done_ = false;

  // Because we don't have backpressure we need an unbounded buffer here :-(
  std::deque<TransformedRequestType> enqueued_requests_;
  bool enqueued_writes_done_ = false;
  bool enqueued_finish_ = false;
  grpc::Status status_;
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
  using TransformedResponse =
      typename decltype(
          Transform::wrap(std::declval<ResponseType>()))::first_type;
};


template <typename Stream, typename ServerCallTraits, typename Callback>
class RsGrpcServerInvocation;

/**
 * Unary server RPC.
 */
template <typename ResponseType, typename ServerCallTraits, typename Callback>
class RsGrpcServerInvocation<
    grpc::ServerAsyncResponseWriter<ResponseType>,
    ServerCallTraits,
    Callback> : public RsGrpcTag {
  using Stream = grpc::ServerAsyncResponseWriter<ResponseType>;
  using Service = typename ServerCallTraits::Service;
  using Transform = typename ServerCallTraits::Transform;
  using TransformedRequest = typename ServerCallTraits::TransformedRequest;
  using TransformedResponse = typename ServerCallTraits::TransformedResponse;

  using Method = RequestMethod<
      Service, typename ServerCallTraits::Request, Stream>;

 public:
  static void Request(
      GrpcErrorHandler error_handler,
      Method method,
      Callback &&callback,
      Service *service,
      grpc::ServerCompletionQueue *cq) {
    auto invocation = new RsGrpcServerInvocation(
        error_handler, method, std::move(callback), service, cq);

    (service->*method)(
        &invocation->context_,
        &invocation->request_,
        &invocation->stream_,
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

    if (awaiting_request_) {
      // The server has just received a request. Handle it.

      auto wrapped_request = ServerCallTraits::Transform::wrap(
          std::move(request_));
      auto values = wrapped_request.second.ok() ?
          Publisher<TransformedResponse>(
              callback_(std::move(wrapped_request.first))) :
          Publisher<TransformedResponse>(
              Throw(std::make_exception_ptr(
                  GrpcError(wrapped_request.second))));

      // Request the a new request, so that the server is always waiting for
      // one. This is done after the callback (because this steals it) but
      // before the subscribe call because that could tell gRPC to respond,
      // after which it's not safe to do anything with `this` anymore.
      IssueNewServerRequest(std::move(callback_));

      awaiting_request_ = false;

      // TODO(peck): Don't hold weak unsafe ref to this
      auto subscription = values.Subscribe(MakeSubscriber(
          [this](TransformedResponse response) {
            num_responses_++;
            response_ = std::move(response);
          },
          [this](std::exception_ptr &&error) {
            stream_.FinishWithError(exceptionToStatus(error), this);
          },
          [this]() {
            if (num_responses_ == 1) {
              stream_.Finish(
                  Transform::unwrap(response_), grpc::Status::OK, this);
            } else {
              const auto *error_message =
                  num_responses_ == 0 ? "No response" : "Too many responses";
              stream_.FinishWithError(
                  grpc::Status(grpc::StatusCode::INTERNAL, error_message),
                  this);
            }
          }));
      // TODO(peck): Backpressure, cancellation
      subscription.Request(ElementCount::Infinite());
    } else {
      // The server has now successfully sent a response. Clean up.
      delete this;
    }
  }

 private:
  RsGrpcServerInvocation(
      GrpcErrorHandler error_handler,
      Method method,
      Callback &&callback,
      Service *service,
      grpc::ServerCompletionQueue *cq)
      : error_handler_(error_handler),
        method_(method),
        callback_(std::move(callback)),
        service_(*service),
        cq_(*cq),
        stream_(&context_) {}

  void IssueNewServerRequest(Callback &&callback) {
    // Take callback as an rvalue parameter to make it obvious that we steal it.
    Request(
        error_handler_,
        method_,
        std::move(callback),  // Reuse the callback functor, don't copy
        &service_,
        &cq_);
  }

  bool awaiting_request_ = true;
  GrpcErrorHandler error_handler_;
  Method method_;
  Callback callback_;
  Service &service_;
  grpc::ServerCompletionQueue &cq_;
  grpc::ServerContext context_;
  typename ServerCallTraits::Request request_;
  Stream stream_;
  int num_responses_ = 0;
  TransformedResponse response_;
};

/**
 * Server streaming.
 */
template <typename ResponseType, typename ServerCallTraits, typename Callback>
class RsGrpcServerInvocation<
    grpc::ServerAsyncWriter<ResponseType>,
    ServerCallTraits,
    Callback> : public RsGrpcTag {
  using Stream = grpc::ServerAsyncWriter<ResponseType>;
  using Service = typename ServerCallTraits::Service;
  using Transform = typename ServerCallTraits::Transform;
  using TransformedRequest = typename ServerCallTraits::TransformedRequest;
  using TransformedResponse = typename ServerCallTraits::TransformedResponse;

  using Method = RequestMethod<
      Service, typename ServerCallTraits::Request, Stream>;

 public:
  static void Request(
      GrpcErrorHandler error_handler,
      Method method,
      Callback &&callback,
      Service *service,
      grpc::ServerCompletionQueue *cq) {
    auto invocation = new RsGrpcServerInvocation(
        error_handler, method, std::move(callback), service, cq);

    (service->*method)(
        &invocation->context_,
        &invocation->request_,
        &invocation->stream_,
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

    switch (state_) {
      case State::AWAITING_REQUEST: {
        // The server has just received a request. Handle it.
        state_ = State::AWAITING_RESPONSE;

        auto wrapped_request = ServerCallTraits::Transform::wrap(
            std::move(request_));
        auto values = wrapped_request.second.ok() ?
            Publisher<TransformedResponse>(
                callback_(std::move(wrapped_request.first))) :
            Publisher<TransformedResponse>(
                Throw(std::make_exception_ptr(
                    GrpcError(wrapped_request.second))));

        // Request the a new request, so that the server is always waiting for
        // one. This is done after the callback (because this steals it) but
        // before the subscribe call because that could tell gRPC to respond,
        // after which it's not safe to do anything with `this` anymore.
        IssueNewServerRequest(std::move(callback_));

        // TODO(peck): Don't hold weak refs to this
        auto subscription = values.Subscribe(MakeSubscriber(
            [this](TransformedResponse &&response) {
              enqueued_responses_.emplace_back(std::move(response));
              RunEnqueuedOperation();
            },
            [this](std::exception_ptr &&error) {
              enqueued_finish_status_ = exceptionToStatus(error);
              enqueued_finish_ = true;
              RunEnqueuedOperation();
            },
            [this]() {
              enqueued_finish_status_ = grpc::Status::OK;
              enqueued_finish_ = true;
              RunEnqueuedOperation();
            }));
      // TODO(peck): Backpressure, cancellation
      subscription.Request(ElementCount::Infinite());

        break;
      }
      case State::AWAITING_RESPONSE:
      case State::SENDING_RESPONSE: {
        state_ = State::AWAITING_RESPONSE;
        RunEnqueuedOperation();
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

  RsGrpcServerInvocation(
      GrpcErrorHandler error_handler,
      Method method,
      Callback &&callback,
      Service *service,
      grpc::ServerCompletionQueue *cq)
      : error_handler_(error_handler),
        method_(method),
        callback_(std::move(callback)),
        service_(*service),
        cq_(*cq),
        stream_(&context_) {}

  void IssueNewServerRequest(Callback &&callback) {
    // Take callback as an rvalue parameter to make it obvious that we steal it.
    Request(
        error_handler_,
        method_,
        std::move(callback),  // Reuse the callback functor, don't copy
        &service_,
        &cq_);
  }

  void RunEnqueuedOperation() {
    if (state_ != State::AWAITING_RESPONSE) {
      return;
    }
    if (!enqueued_responses_.empty()) {
      state_ = State::SENDING_RESPONSE;
      stream_.Write(
          Transform::unwrap(std::move(enqueued_responses_.front())), this);
      enqueued_responses_.pop_front();
    } else if (enqueued_finish_) {
      enqueued_finish_ = false;
      state_ = State::SENT_FINAL_RESPONSE;
      stream_.Finish(enqueued_finish_status_, this);
    }
  }

  State state_ = State::AWAITING_REQUEST;
  bool enqueued_finish_ = false;
  grpc::Status enqueued_finish_status_;
  std::deque<TransformedResponse> enqueued_responses_;

  GrpcErrorHandler error_handler_;
  Method method_;
  Callback callback_;
  Service &service_;
  grpc::ServerCompletionQueue &cq_;
  grpc::ServerContext context_;
  typename ServerCallTraits::Request request_;
  Stream stream_;
};

/**
 * Client streaming.
 */
template <
    typename ResponseType,
    typename RequestType,
    typename ServerCallTraits,
    typename Callback>
class RsGrpcServerInvocation<
    grpc::ServerAsyncReader<ResponseType, RequestType>,
    ServerCallTraits,
    Callback> : public RsGrpcTag {
  using Stream = grpc::ServerAsyncReader<ResponseType, RequestType>;
  using Service = typename ServerCallTraits::Service;
  using Transform = typename ServerCallTraits::Transform;
  using TransformedRequest = typename ServerCallTraits::TransformedRequest;
  using TransformedResponse = typename ServerCallTraits::TransformedResponse;

  using Method = StreamingRequestMethod<Service, Stream>;

 public:
  static void Request(
      GrpcErrorHandler error_handler,
      Method method,
      Callback &&callback,
      Service *service,
      grpc::ServerCompletionQueue *cq) {
    auto invocation = new RsGrpcServerInvocation(
        error_handler, method, std::move(callback), service, cq);

    (service->*method)(
        &invocation->context_,
        &invocation->reader_,
        cq,
        cq,
        invocation);
  }

  void operator()(bool success) {
    switch (state_) {
      case State::INIT: {
        if (!success) {
          delete this;
        } else {
          // Need to set _state before the call to init, in case it moves on to
          // the State::REQUESTED_DATA state immediately.
          state_ = State::INITIALIZED;
          Init();
        }
        break;
      }
      case State::INITIALIZED: {
        abort();  // Should not get here
        break;
      }
      case State::REQUESTED_DATA: {
        if (success) {
          auto wrapped = Transform::wrap(std::move(request_));
          if (wrapped.second.ok()) {
            subscriber_->OnNext(std::move(wrapped.first));
            reader_.Read(&request_, this);
          } else {
            subscriber_->OnError(
                std::make_exception_ptr(GrpcError(wrapped.second)));

            state_ = State::SENT_RESPONSE;
            reader_.FinishWithError(wrapped.second, this);
          }
        } else {
          // The client has stopped sending requests.
          subscriber_->OnComplete();
          state_ = State::STREAM_ENDED;
          TrySendResponse();
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

  RsGrpcServerInvocation(
      GrpcErrorHandler error_handler,
      Method method,
      Callback &&callback,
      Service *service,
      grpc::ServerCompletionQueue *cq)
      : error_handler_(error_handler),
        method_(method),
        callback_(std::move(callback)),
        service_(*service),
        cq_(*cq),
        reader_(&context_) {}

  void Init() {
    auto response = callback_(Publisher<TransformedRequest>(MakePublisher(
        [this](auto &&subscriber) {
      if (subscriber_) {
        throw std::logic_error(
            "Can't subscribe to this observable more than once");
      }
      subscriber_.reset(
          new Subscriber<TransformedRequest>(
              std::forward<decltype(subscriber)>(subscriber)));

      state_ = State::REQUESTED_DATA;
      reader_.Read(&request_, this);

      return MakeSubscription();  // TODO(peck): Do something sane here
    })));

    static_assert(
        IsPublisher<decltype(response)>,
        "Callback return type must be Publisher");
    // TODO(peck): Don't hold weak unsafe refs to this
    auto subscription = response.Subscribe(MakeSubscriber(
        [this](TransformedResponse response) {
          response_ = std::move(response);
          num_responses_++;
        },
        [this](std::exception_ptr &&error) {
          response_error_ = error;
          finished_ = true;
          TrySendResponse();
        },
        [this]() {
          finished_ = true;
          TrySendResponse();
        }));
    // TODO(peck): Backpressure, cancellation
    subscription.Request(ElementCount::Infinite());

    // Request the a new request, so that the server is always waiting for
    // one.
    Request(
        error_handler_,
        method_,
        std::move(callback_),  // Reuse the callback functor, don't copy
        &service_,
        &cq_);
  }

  void IssueNewServerRequest(std::unique_ptr<Callback> &&callback) {
  }

  void TrySendResponse() {
    if (finished_ && state_ == State::STREAM_ENDED) {
      state_ = State::SENT_RESPONSE;
      if (response_error_) {
        reader_.FinishWithError(exceptionToStatus(response_error_), this);
      } else if (num_responses_ == 1) {
        reader_.Finish(
            Transform::unwrap(response_),
            grpc::Status::OK,
            this);
      } else {
        const auto *error_message =
            num_responses_ == 0 ? "No response" : "Too many responses";
        reader_.FinishWithError(
            grpc::Status(grpc::StatusCode::INTERNAL, error_message),
            this);
      }
    }
  }

  // TODO(peck): Consider avoiding type erasure here
  std::unique_ptr<Subscriber<TransformedRequest>> subscriber_;
  State state_ = State::INIT;
  GrpcErrorHandler error_handler_;
  Method method_;
  Callback callback_;
  Service &service_;
  grpc::ServerCompletionQueue &cq_;
  grpc::ServerContext context_;
  typename ServerCallTraits::Request request_;
  grpc::ServerAsyncReader<ResponseType, RequestType> reader_;

  TransformedResponse response_;
  int num_responses_ = 0;

  std::exception_ptr response_error_;
  bool finished_ = false;
};

/**
 * Bidi streaming.
 */
template <
    typename ResponseType,
    typename RequestType,
    typename ServerCallTraits,
    typename Callback>
class RsGrpcServerInvocation<
    grpc::ServerAsyncReaderWriter<ResponseType, RequestType>,
    ServerCallTraits,
    Callback> : public RsGrpcTag {
  using Stream = grpc::ServerAsyncReaderWriter<ResponseType, RequestType>;
  using Service = typename ServerCallTraits::Service;
  using Transform = typename ServerCallTraits::Transform;
  using TransformedRequest = typename ServerCallTraits::TransformedRequest;

  using ResponsePublisher =
      decltype(std::declval<Callback>()(
          std::declval<Publisher<TransformedRequest>>()));
  using TransformedResponse = typename ResponsePublisher::value_type;

  using Method = StreamingRequestMethod<Service, Stream>;

  /**
   * Bidi streaming requires separate tags for reading and writing (since they
   * can happen simultaneously). The purpose of this class is to be the other
   * tag. It's used for writing.
   */
  class Writer : public RsGrpcTag {
   public:
    Writer(
        const std::function<void ()> shutdown,
        grpc::ServerContext *context,
        grpc::ServerAsyncReaderWriter<ResponseType, RequestType> *stream)
        : shutdown_(shutdown),
          context_(*context),
          stream_(*stream) {}

    template <typename Publisher>
    void Subscribe(const Publisher &publisher) {
      publisher.subscribe(
          [this](auto &&response) {
            enqueued_responses_.emplace_back(
                std::forward<decltype(response)>(response));
            RunEnqueuedOperation();
          },
          [this](std::exception_ptr &&error) {
            OnError(error);
          },
          [this]() {
            enqueued_finish_ = true;
            RunEnqueuedOperation();
          });
    }

    /**
     * Try to end the write stream with an error. If the write stream has
     * already finished, this is a no-op.
     */
    void OnError(const std::exception_ptr &error) {
      status_ = exceptionToStatus(error);
      enqueued_finish_ = true;
      RunEnqueuedOperation();
    }

    void operator()(bool success) override {
      if (sent_final_request_) {
        // Nothing more to write.
        shutdown_();
      } else {
        if (success) {
          operation_in_progress_ = false;
          RunEnqueuedOperation();
        } else {
          // This happens when the runloop is shutting down.
          shutdown_();
        }
      }
    }

   private:
    void RunEnqueuedOperation() {
      if (operation_in_progress_) {
        return;
      }
      if (!enqueued_responses_.empty()) {
        operation_in_progress_ = true;
        stream_.Write(
            Transform::unwrap(std::move(enqueued_responses_.front())), this);
        enqueued_responses_.pop_front();
      } else if (enqueued_finish_ && !sent_final_request_) {
        enqueued_finish_ = false;
        operation_in_progress_ = true;

        // Must be done before the call to Finish because it's not safe to do
        // anything after that call; gRPC could invoke the callback immediately
        // on another thread, which could delete this.
        sent_final_request_ = true;

        stream_.Finish(status_, this);
      }
    }

    std::function<void ()> shutdown_;
    // Because we don't have backpressure we need an unbounded buffer here :-(
    std::deque<TransformedResponse> enqueued_responses_;
    bool enqueued_finish_ = false;
    bool operation_in_progress_ = false;
    bool sent_final_request_ = false;
    grpc::ServerContext &context_;
    grpc::ServerAsyncReaderWriter<ResponseType, RequestType> &stream_;
    grpc::Status status_;
  };

 public:
  static void Request(
      GrpcErrorHandler error_handler,
      Method method,
      Callback &&callback,
      Service *service,
      grpc::ServerCompletionQueue *cq) {
    auto invocation = new RsGrpcServerInvocation(
        error_handler, method, std::move(callback), service, cq);

    (service->*method)(
        &invocation->context_,
        &invocation->stream_,
        cq,
        cq,
        invocation);
  }

  void operator()(bool success) {
    switch (state_) {
      case State::INIT: {
        if (!success) {
          delete this;
        } else {
          // Need to set _state before the call to init, in case it moves on to
          // the State::REQUESTED_DATA state immediately.
          state_ = State::INITIALIZED;
          Init();
        }
        break;
      }
      case State::INITIALIZED: {
        abort();  // Should not get here
        break;
      }
      case State::REQUESTED_DATA: {
        if (success) {
          auto wrapped = Transform::wrap(std::move(request_));
          if (wrapped.second.ok()) {
            subscriber_->on_next(std::move(wrapped.first));
            stream_.Read(&request_, this);
          } else {
            auto error = std::make_exception_ptr(GrpcError(wrapped.second));
            subscriber_->on_error(error);

            writer_.OnError(error);
            state_ = State::READ_STREAM_ENDED;
          }
        } else {
          // The client has stopped sending requests.
          subscriber_->on_completed();
          state_ = State::READ_STREAM_ENDED;
          TryShutdown();
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

  RsGrpcServerInvocation(
      GrpcErrorHandler error_handler,
      Method method,
      Callback &&callback,
      Service *service,
      grpc::ServerCompletionQueue *cq)
      : error_handler_(error_handler),
        method_(method),
        callback_(std::move(callback)),
        service_(*service),
        cq_(*cq),
        stream_(&context_),
        writer_(
            [this] { write_stream_ended_ = true; TryShutdown(); },
            &context_,
            &stream_) {}

  void TryShutdown() {
    if (state_ == State::READ_STREAM_ENDED && write_stream_ended_) {
      // Only delete this when both the read stream and the write stream have
      // finished.
      delete this;
    }
  }

  void Init() {
    auto response = callback_(Publisher<TransformedRequest>(MakePublisher(
        [this](auto &&subscriber) {
      if (subscriber_) {
        throw std::logic_error(
            "Can't subscribe to this Publisher more than once");
      }
      subscriber_.reset(
          new Subscriber<TransformedRequest>(
              std::forward<decltype(subscriber)>(subscriber)));

      state_ = State::REQUESTED_DATA;
      stream_.Read(&request_, this);

      return MakeSubscription();  // TODO(peck): Do something sane here
    })));

    static_assert(
        IsPublisher<decltype(response)>,
        "Callback return type must be Publisher");
    writer_.Subscribe(response);

    Request(
        error_handler_,
        method_,
        std::move(callback_),  // Reuse the callback functor, don't copy
        &service_,
        &cq_);
  }

  // TODO(peck): Consider avoiding type erasure here
  std::unique_ptr<Subscriber<TransformedRequest>> subscriber_;
  State state_ = State::INIT;
  GrpcErrorHandler error_handler_;
  Method method_;
  Callback callback_;
  Service &service_;
  grpc::ServerCompletionQueue &cq_;
  grpc::ServerContext context_;
  typename ServerCallTraits::Request request_;
  grpc::ServerAsyncReaderWriter<ResponseType, RequestType> stream_;
  bool write_stream_ended_ = false;
  Writer writer_;

  TransformedResponse response_;
  std::exception_ptr response_error_;
};

class InvocationRequester {
 public:
  virtual ~InvocationRequester() = default;

  virtual void RequestInvocation(
      GrpcErrorHandler error_handler,
      grpc::ServerCompletionQueue *cq) = 0;
};

template <
    // RequestMethod<Service, RequestType, Stream> or
    // StreamingRequestMethod<Service, Stream>
    typename Method,
    typename ServerCallTraits,
    typename Callback>
class RsGrpcServerInvocationRequester : public InvocationRequester {
  using Service = typename ServerCallTraits::Service;

 public:
  RsGrpcServerInvocationRequester(
      Method method, Callback &&callback, Service *service)
      : method_(method), callback_(std::move(callback)), service_(*service) {}

  void RequestInvocation(
      GrpcErrorHandler error_handler,
      grpc::ServerCompletionQueue *cq) override {
    using ServerInvocation = RsGrpcServerInvocation<
        typename ServerCallTraits::Stream,
        ServerCallTraits,
        Callback>;
    ServerInvocation::Request(
        error_handler, method_, Callback(callback_), &service_, cq);
  }

 private:
  Method method_;
  Callback callback_;
  Service &service_;
};

}  // namespace detail

template <typename Stub, typename Transform>
class RsGrpcServiceClient {
 public:
  RsGrpcServiceClient(std::unique_ptr<Stub> &&stub, grpc::CompletionQueue *cq)
      : stub_(std::move(stub)), cq_(*cq) {}

  /**
   * Unary rpc.
   */
  template <typename ResponseType, typename TransformedRequestType>
  Publisher<
      typename detail::RsGrpcClientInvocation<
          grpc::ClientAsyncResponseReader<ResponseType>,
          ResponseType,
          TransformedRequestType,
          Transform>::TransformedResponseType>
  Invoke(
      std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>>
      (Stub::*invoke)(
          grpc::ClientContext *context,
          const decltype(Transform::unwrap(
              std::declval<TransformedRequestType>())) &request,
          grpc::CompletionQueue *cq),
      const TransformedRequestType &request,
      grpc::ClientContext &&context = grpc::ClientContext()) {
    return InvokeImpl<
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
  Publisher<
      typename detail::RsGrpcClientInvocation<
          grpc::ClientAsyncReader<ResponseType>,
          ResponseType,
          TransformedRequestType,
          Transform>::TransformedResponseType>
  Invoke(
      std::unique_ptr<grpc::ClientAsyncReader<ResponseType>>
      (Stub::*invoke)(
          grpc::ClientContext *context,
          const decltype(Transform::unwrap(
              std::declval<TransformedRequestType>())) &request,
          grpc::CompletionQueue *cq,
          void *tag),
      const TransformedRequestType &request,
      grpc::ClientContext &&context = grpc::ClientContext()) {
    return InvokeImpl<
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
      typename TransformedRequestType>
  Publisher<
      typename detail::RsGrpcClientInvocation<
          grpc::ClientAsyncWriter<RequestType>,
          ResponseType,
          TransformedRequestType,
          Transform>::TransformedResponseType>
  Invoke(
      std::unique_ptr<grpc::ClientAsyncWriter<RequestType>>
      (Stub::*invoke)(
          grpc::ClientContext *context,
          ResponseType *response,
          grpc::CompletionQueue *cq,
          void *tag),
      // TODO(peck): This should not require a type erased Publisher (with
      // rxcpp it didn't). What to do?
      Publisher<TransformedRequestType> &&requests,
      grpc::ClientContext &&context = grpc::ClientContext()) {
    return InvokeImpl<
        grpc::ClientAsyncWriter<RequestType>,
        ResponseType,
        TransformedRequestType>(
            invoke,
            std::move(requests),
            std::move(context));
  }

  /**
   * Bidi streaming
   */
  template <
      typename RequestType,
      typename ResponseType,
      typename TransformedRequestType>
  Publisher<
      typename detail::RsGrpcClientInvocation<
          grpc::ClientAsyncReaderWriter<RequestType, ResponseType>,
          ResponseType,
          TransformedRequestType,
          Transform>::TransformedResponseType>
  Invoke(
      std::unique_ptr<grpc::ClientAsyncReaderWriter<RequestType, ResponseType>>
      (Stub::*invoke)(
          grpc::ClientContext *context,
          grpc::CompletionQueue *cq,
          void *tag),
      // TODO(peck): This should not require a type erased Publisher (with
      // rxcpp it didn't). What to do?
      Publisher<TransformedRequestType> &&requests,
      grpc::ClientContext &&context = grpc::ClientContext()) {
    return InvokeImpl<
        grpc::ClientAsyncReaderWriter<RequestType, ResponseType>,
        ResponseType,
        TransformedRequestType>(
            invoke,
            std::move(requests),
            std::move(context));
  }

 private:
  template <
      typename Reader,
      typename ResponseType,
      typename TransformedRequestType,
      typename RequestOrPublisher,
      typename Invoke>
  auto InvokeImpl(
      Invoke invoke,
      RequestOrPublisher &&request_or_publisher,
      grpc::ClientContext &&context = grpc::ClientContext()) {

    using ClientInvocation =
        detail::RsGrpcClientInvocation<
            Reader, ResponseType, TransformedRequestType, Transform>;

    using ErasedPublisher =
        Publisher<
            typename detail::RsGrpcClientInvocation<
                Reader,
                ResponseType,
                TransformedRequestType,
                Transform>::TransformedResponseType>;

    return ErasedPublisher(MakePublisher([
        this,
        request_or_publisher =
            std::forward<RequestOrPublisher>(request_or_publisher),
        invoke](auto &&subscriber) {
      auto call = new ClientInvocation(
          request_or_publisher,
          std::forward<decltype(subscriber)>(subscriber));
      return call->Invoke(invoke, stub_.get(), &cq_);
    }));
  }

  std::unique_ptr<Stub> stub_;
  grpc::CompletionQueue &cq_;
};

class RsGrpcServer {
  using ServiceRef = std::unique_ptr<void, std::function<void (void *)>>;
  using Services = std::vector<ServiceRef>;
 public:
  RsGrpcServer(
      Services &&services,
      std::unique_ptr<grpc::ServerCompletionQueue> &&cq,
      std::unique_ptr<grpc::Server> &&server)
      : services_(std::move(services)),
        cq_(std::move(cq)),
        server_(std::move(server)) {}

  RsGrpcServer(RsGrpcServer &&) = default;
  RsGrpcServer &operator=(RsGrpcServer &&) = default;

  ~RsGrpcServer() {
    Shutdown();
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
          : service_(*service),
            invocation_requesters_(*requesters) {}

      // Unary RPC
      template <
          typename Transform = detail::RsGrpcIdentityTransform,
          typename InnerService,
          typename ResponseType,
          typename RequestType,
          typename Callback>
      ServiceBuilder &RegisterMethod(
          detail::RequestMethod<
              InnerService,
              RequestType,
              grpc::ServerAsyncResponseWriter<ResponseType>> method,
          Callback &&callback) {
        RegisterMethodImpl<
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
          typename Transform = detail::RsGrpcIdentityTransform,
          typename InnerService,
          typename ResponseType,
          typename RequestType,
          typename Callback>
      ServiceBuilder &RegisterMethod(
          detail::RequestMethod<
              InnerService,
              RequestType,
              grpc::ServerAsyncWriter<ResponseType>> method,
          Callback &&callback) {
        RegisterMethodImpl<
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
          typename Transform = detail::RsGrpcIdentityTransform,
          typename InnerService,
          typename ResponseType,
          typename RequestType,
          typename Callback>
      ServiceBuilder &RegisterMethod(
          detail::StreamingRequestMethod<
              InnerService,
              grpc::ServerAsyncReader<ResponseType, RequestType>> method,
          Callback &&callback) {
        RegisterMethodImpl<
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
          typename Transform = detail::RsGrpcIdentityTransform,
          typename InnerService,
          typename ResponseType,
          typename RequestType,
          typename Callback>
      ServiceBuilder &RegisterMethod(
          detail::StreamingRequestMethod<
              InnerService,
              grpc::ServerAsyncReaderWriter<ResponseType, RequestType>> method,
          Callback &&callback) {
        RegisterMethodImpl<
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
      void RegisterMethodImpl(
          Method method,
          Callback &&callback) {
        using ServerInvocationRequester =
            detail::RsGrpcServerInvocationRequester<
                Method,
                ServerCallTraits,
                Callback>;

        invocation_requesters_.emplace_back(
            new ServerInvocationRequester(
                method, std::move(callback), &service_));
      }

      Service &service_;
      std::vector<std::unique_ptr<detail::InvocationRequester>> &
          invocation_requesters_;
    };

    template <typename Service>
    ServiceBuilder<Service> RegisterService() {
      auto service = new Service();
      services_.emplace_back(service, [](void *service) {
        delete reinterpret_cast<Service *>(service);
      });
      builder_.RegisterService(service);
      return ServiceBuilder<Service>(service, &invocation_requesters_);
    }

    grpc::ServerBuilder &GrpcServerBuilder() {
      return builder_;
    }

    /**
     * Build and start the gRPC server. After calling this method this object is
     * dead and the only valid operation on it is to destroy it.
     */
    RsGrpcServer BuildAndStart() {
      RsGrpcServer server(
          std::move(services_),
          builder_.AddCompletionQueue(),
          builder_.BuildAndStart());

      for (const auto &requester: invocation_requesters_) {
        requester->RequestInvocation(error_handler_, server.cq_.get());
      }

      return server;
    }

   private:
    GrpcErrorHandler error_handler_ = [](std::exception_ptr error) {
      std::rethrow_exception(error);
    };
    Services services_;
    std::vector<std::unique_ptr<detail::InvocationRequester>>
        invocation_requesters_;
    grpc::ServerBuilder builder_;
  };

  template <
      typename Transform = detail::RsGrpcIdentityTransform,
      typename Stub>
  RsGrpcServiceClient<Stub, Transform> MakeClient(
      std::unique_ptr<Stub> &&stub) {
    return RsGrpcServiceClient<Stub, Transform>(std::move(stub), cq_.get());
  }

  /**
   * Block and process asynchronous events until the server is shut down.
   */
  void Run() {
    return detail::RsGrpcTag::processAllEvents(cq_.get());
  }

  /**
   * Block and process one asynchronous event.
   *
   * Returns false if the event queue is shutting down.
   */
  bool Next() {
    return detail::RsGrpcTag::processOneEvent(cq_.get());
  }

  void Shutdown() {
    // _server and _cq might be nullptr if this object has been moved out from.
    if (server_) {
      server_->Shutdown();
    }
    if (cq_) {
      cq_->Shutdown();
    }
  }

 private:
  // This object doesn't really do anything with the services other than owning
  // them, so that they are valid while the server is servicing requests and
  // that they can be destroyed at the right time.
  Services services_;
  std::unique_ptr<grpc::ServerCompletionQueue> cq_;
  std::unique_ptr<grpc::Server> server_;
};

class RsGrpcClient {
 public:
  ~RsGrpcClient() {
    Shutdown();
  }

  template <
      typename Transform = detail::RsGrpcIdentityTransform,
      typename Stub>
  RsGrpcServiceClient<Stub, Transform> MakeClient(
      std::unique_ptr<Stub> &&stub) {
    return RsGrpcServiceClient<Stub, Transform>(std::move(stub), &cq_);
  }

  /**
   * Block and process asynchronous events until the server is shut down.
   */
  void Run() {
    return detail::RsGrpcTag::processAllEvents(&cq_);
  }

  /**
   * Block and process one asynchronous event.
   *
   * Returns false if the event queue is shutting down.
   */
  bool Next() {
    return detail::RsGrpcTag::processOneEvent(&cq_);
  }

  void Shutdown() {
    cq_.Shutdown();
  }

 private:
  grpc::CompletionQueue cq_;
};
}  // namespace shk
