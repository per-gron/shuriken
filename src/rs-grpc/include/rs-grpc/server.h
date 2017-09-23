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
#include <vector>

#include <grpc++/grpc++.h>

#include <rs/publisher.h>
#include <rs/subscriber.h>
#include <rs/subscription.h>
#include <rs/throw.h>
#include <rs-grpc/detail/rs_grpc_tag.h>
#include <rs-grpc/detail/subscriber.h>
#include <rs-grpc/detail/subscription.h>
#include <rs-grpc/call_context.h>
#include <rs-grpc/client.h>
#include <rs-grpc/grpc_error.h>

namespace shk {
namespace detail {

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
    typename Callback>
class ServerCallTraits {
 public:
  using Stream = StreamType;
  using Service = ServiceType;
  using Response = ResponseType;
  using Request = RequestType;
};


template <typename Stream, typename ServerCallTraits, typename Callback>
class RsGrpcServerCall;

/**
 * Unary server RPC.
 */
template <typename ResponseType, typename ServerCallTraits, typename Callback>
class RsGrpcServerCall<
    grpc::ServerAsyncResponseWriter<ResponseType>,
    ServerCallTraits,
    Callback> : public RsGrpcTag, public Subscriber {
  using Stream = grpc::ServerAsyncResponseWriter<ResponseType>;
  using Service = typename ServerCallTraits::Service;

  using Method = RequestMethod<
      Service, typename ServerCallTraits::Request, Stream>;

 public:
  static void Request(
      GrpcErrorHandler error_handler,
      Method method,
      Callback &&callback,
      Service *service,
      grpc::ServerCompletionQueue *cq) {
    auto call = RsGrpcTag::Ptr<RsGrpcServerCall>::TakeOver(
        new RsGrpcServerCall(
            error_handler, method, std::move(callback), service, cq));

    (service->*method)(
        &call->context_,
        &call->request_,
        &call->stream_,
        cq,
        cq,
        call->ToTag());
  }

  void OnNext(ResponseType &&response) {
    num_responses_++;
    response_ = std::move(response);
  }

  void OnError(std::exception_ptr &&error) {
    stream_.FinishWithError(ExceptionToStatus(error), ToTag());
  }

  void OnComplete() {
    if (num_responses_ == 1) {
      stream_.Finish(response_, grpc::Status::OK, ToTag());
    } else {
      const auto *error_message =
          num_responses_ == 0 ? "No response" : "Too many responses";
      stream_.FinishWithError(
          grpc::Status(grpc::StatusCode::INTERNAL, error_message),
          ToTag());
    }
  }

 protected:
  void TagOperationDone(bool success) {
    if (!success) {
      // This happens when the server is shutting down.
      return;
    }

    if (awaiting_request_) {
      // The server has just received a request. Handle it.

      auto values = callback_(CallContext(), std::move(request_));

      // Request the a new request, so that the server is always waiting for
      // one. This is done after the callback (because this steals it) but
      // before the subscribe call because that could tell gRPC to respond,
      // after which it's not safe to do anything with `this` anymore.
      IssueNewServerRequest(std::move(callback_));

      awaiting_request_ = false;

      // TODO(peck): Handle cancellation
      auto subscription = values.Subscribe(
          MakeRsGrpcTagSubscriber(ToShared(this)));
      // Because this class only uses the first response (and fails if there
      // are more), it's fine to Request an unbounded number of elements from
      // this stream; all elements after the first are immediately discarded.
      subscription.Request(ElementCount::Unbounded());
    } else {
      // The server has now successfully sent a response.
    }
  }

 private:
  RsGrpcServerCall(
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
  ResponseType response_;
};

/**
 * Server streaming.
 */
template <typename ResponseType, typename ServerCallTraits, typename Callback>
class RsGrpcServerCall<
    grpc::ServerAsyncWriter<ResponseType>,
    ServerCallTraits,
    Callback> : public RsGrpcTag, public Subscriber {
  using Stream = grpc::ServerAsyncWriter<ResponseType>;
  using Service = typename ServerCallTraits::Service;

  using Method = RequestMethod<
      Service, typename ServerCallTraits::Request, Stream>;

 public:
  static void Request(
      GrpcErrorHandler error_handler,
      Method method,
      Callback &&callback,
      Service *service,
      grpc::ServerCompletionQueue *cq) {
    auto call = RsGrpcTag::Ptr<RsGrpcServerCall>::TakeOver(
        new RsGrpcServerCall(
            error_handler, method, std::move(callback), service, cq));

    (service->*method)(
        &call->context_,
        &call->request_,
        &call->stream_,
        cq,
        cq,
        call->ToTag());
  }

  void OnNext(ResponseType &&response) {
    if (next_response_) {
      next_response_.reset();
      OnError(std::make_exception_ptr(
          std::logic_error("Backpressure violation")));
    } else {
      next_response_ = std::make_unique<ResponseType>(std::move(response));
      RunEnqueuedOperation();
    }
  }

  void OnError(std::exception_ptr &&error) {
    enqueued_finish_status_ = ExceptionToStatus(error);
    enqueued_finish_ = true;
    RunEnqueuedOperation();
  }

  void OnComplete() {
    enqueued_finish_status_ = grpc::Status::OK;
    enqueued_finish_ = true;
    RunEnqueuedOperation();
  }

 protected:
  void TagOperationDone(bool success) {
    if (!success) {
      // This happens when the server is shutting down.
      subscription_ = AnySubscription();
      retained_self_.Reset();
      return;
    }

    switch (state_) {
      case State::AWAITING_REQUEST: {
        // The server has just received a request. Handle it.
        state_ = State::AWAITING_RESPONSE;

        auto values = callback_(CallContext(), std::move(request_));

        // Request the a new request, so that the server is always waiting for
        // one. This is done after the callback (because this steals it) but
        // before the subscribe call because that could tell gRPC to respond,
        // after which it's not safe to do anything with `this` anymore.
        IssueNewServerRequest(std::move(callback_));

        // Subscribe to the values Publisher. In doing so, we keep the returned
        // Subscription. Because the Subscription can own the Subscriber that
        // we give to Subscribe, the Subscriber cannot have an owning reference
        // to this, becuase then we get a reference cycle.
        //
        // However, we can't just give a Subscriber with a weak reference to
        // this, because then if a value is emitted asynchronously, this object
        // might have been destroyed.
        //
        // In order to solve this, this object retains itself until the stream
        // finishes or the request is cancelled.
        retained_self_ = ToShared(this);
        subscription_ = AnySubscription(values.Subscribe(
            MakeRsGrpcTagSubscriber(ToWeak(this))));
        // TODO(peck): Cancellation
        subscription_.Request(ElementCount(1));

        break;
      }
      case State::AWAITING_RESPONSE:
      case State::SENDING_RESPONSE: {
        state_ = State::AWAITING_RESPONSE;
        RunEnqueuedOperation();
        break;
      }
      case State::SENT_FINAL_RESPONSE: {
        subscription_ = AnySubscription();
        retained_self_.Reset();
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

  RsGrpcServerCall(
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
    if (next_response_) {
      state_ = State::SENDING_RESPONSE;
      stream_.Write(*next_response_, ToTag());
      next_response_.reset();
      subscription_.Request(ElementCount(1));
    } else if (enqueued_finish_) {
      enqueued_finish_ = false;
      state_ = State::SENT_FINAL_RESPONSE;
      stream_.Finish(enqueued_finish_status_, ToTag());
    }
  }

  State state_ = State::AWAITING_REQUEST;
  bool enqueued_finish_ = false;
  grpc::Status enqueued_finish_status_;
  AnySubscription subscription_;
  std::unique_ptr<ResponseType> next_response_;

  Ptr<RsGrpcServerCall> retained_self_;
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
class RsGrpcServerCall<
    grpc::ServerAsyncReader<ResponseType, RequestType>,
    ServerCallTraits,
    Callback>
        : public RsGrpcTag, public Subscriber, public Subscription {
  using Stream = grpc::ServerAsyncReader<ResponseType, RequestType>;
  using Service = typename ServerCallTraits::Service;
  using Method = StreamingRequestMethod<Service, Stream>;

 public:
  static void Request(
      GrpcErrorHandler error_handler,
      Method method,
      Callback &&callback,
      Service *service,
      grpc::ServerCompletionQueue *cq) {
    auto call = RsGrpcTag::Ptr<RsGrpcServerCall>::TakeOver(
        new RsGrpcServerCall(
            error_handler, method, std::move(callback), service, cq));

    (service->*method)(
        &call->context_,
        &call->reader_,
        cq,
        cq,
        call->ToTag());
  }

  void OnNext(ResponseType &&response) {
    response_ = std::move(response);
    num_responses_++;
  }

  void OnError(std::exception_ptr &&error) {
    response_error_ = error;
    finished_ = true;
    TrySendResponse();
  }

  void OnComplete() {
    finished_ = true;
    TrySendResponse();
  }

  void Request(ElementCount count) {
    requested_ += count;
    MaybeReadNext();
  }

  void Cancel() {
    // TODO(peck): Handle cancellation
  }

 protected:
  void TagOperationDone(bool success) {
    switch (state_) {
      case State::INIT: {
        if (!success) {
          // The runloop was shut down before a request was received. There is
          // nothing to do here.
        } else {
          // Need to set _state before the call to init, in case it moves on to
          // the State::REQUESTED_DATA state immediately.
          state_ = State::WAITING_FOR_DATA_REQUEST;
          Init();
        }
        break;
      }
      case State::WAITING_FOR_DATA_REQUEST: {
        abort();  // Should not get here
        break;
      }
      case State::REQUESTED_DATA: {
        if (success) {
          subscriber_->OnNext(std::move(request_));
          state_ = State::WAITING_FOR_DATA_REQUEST;
          MaybeReadNext();
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
        break;
      }
    }
  }

 private:
  enum class State {
    INIT,
    WAITING_FOR_DATA_REQUEST,
    REQUESTED_DATA,
    STREAM_ENDED,
    SENT_RESPONSE
  };

  RsGrpcServerCall(
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
    auto response = callback_(
        CallContext(),
        AnyPublisher<RequestType>(MakePublisher(
            [self = ToShared(this)](auto &&subscriber) {
          if (self->subscriber_) {
            throw std::logic_error(
                "Can't subscribe to this observable more than once");
          }
          self->subscriber_.reset(
              new AnySubscriber<RequestType>(
                  std::forward<decltype(subscriber)>(subscriber)));

          return MakeRsGrpcTagSubscription(self);
        })));

    static_assert(
        IsPublisher<decltype(response)>,
        "Callback return type must be Publisher");
    // TODO(peck): Handle cancellation
    // TODO(peck): I think it's wrong for this Subscriber to hold a weak
    // reference to this. I think it needs to be a strong ref. But that
    // would cause a cyclic ref it seems...?
    auto subscription = AnySubscription(response.Subscribe(
        MakeRsGrpcTagSubscriber(ToWeak(this))));
    // Because this class only uses the first response (and fails if there are
    // more), it's fine to Request an unbounded number of elements from this
    // stream; all elements after the first are immediately discarded.
    subscription.Request(ElementCount::Unbounded());

    // Request the a new request, so that the server is always waiting for
    // one.
    Request(
        error_handler_,
        method_,
        std::move(callback_),  // Reuse the callback functor, don't copy
        &service_,
        &cq_);
  }

  void MaybeReadNext() {
    if (requested_ > 0 && state_ == State::WAITING_FOR_DATA_REQUEST) {
      --requested_;
      state_ = State::REQUESTED_DATA;
      reader_.Read(&request_, ToTag());
    }
  }

  void TrySendResponse() {
    if (finished_ && state_ == State::STREAM_ENDED) {
      state_ = State::SENT_RESPONSE;
      if (response_error_) {
        reader_.FinishWithError(ExceptionToStatus(response_error_), ToTag());
      } else if (num_responses_ == 1) {
        reader_.Finish(response_, grpc::Status::OK, ToTag());
      } else {
        const auto *error_message =
            num_responses_ == 0 ? "No response" : "Too many responses";
        reader_.FinishWithError(
            grpc::Status(grpc::StatusCode::INTERNAL, error_message),
            ToTag());
      }
    }
  }

  // The number of elements that have been requested by the subscriber that have
  // not yet been requested to be read from gRPC.
  ElementCount requested_;

  std::unique_ptr<AnySubscriber<RequestType>> subscriber_;
  State state_ = State::INIT;
  GrpcErrorHandler error_handler_;
  Method method_;
  Callback callback_;
  Service &service_;
  grpc::ServerCompletionQueue &cq_;
  grpc::ServerContext context_;
  typename ServerCallTraits::Request request_;
  grpc::ServerAsyncReader<ResponseType, RequestType> reader_;

  ResponseType response_;
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
class RsGrpcServerCall<
    grpc::ServerAsyncReaderWriter<ResponseType, RequestType>,
    ServerCallTraits,
    Callback>
        : public RsGrpcTag, public Subscriber, public Subscription {
  using Stream = grpc::ServerAsyncReaderWriter<ResponseType, RequestType>;
  using Service = typename ServerCallTraits::Service;

  using Method = StreamingRequestMethod<Service, Stream>;

  /**
   * The purpose of this class is to encapsulate the logic for writing from
   * the rest of this RsGrpcServerCall template specialization.
   */
  class Writer {
   public:
    Writer(
        RsGrpcServerCall *parent,
        grpc::ServerContext *context,
        grpc::ServerAsyncReaderWriter<ResponseType, RequestType> *stream)
        : parent_(*parent),
          context_(*context),
          stream_(*stream) {}

    void Subscribed(AnySubscription &&subscription) {
      subscription_ = std::move(subscription);
      // TODO(peck): Cancellation
      subscription_.Request(ElementCount(1));
    }

    void OnNext(ResponseType &&response) {
      if (next_response_) {
        next_response_.reset();
        OnError(std::make_exception_ptr(
            std::logic_error("Backpressure violation")));
      } else {
        next_response_ = std::make_unique<ResponseType>(
            std::forward<decltype(response)>(response));
        RunEnqueuedOperation();
      }
    }

    /**
     * Try to end the write stream with an error. If the write stream has
     * already finished, this is a no-op.
     */
    void OnError(std::exception_ptr &&error) {
      status_ = ExceptionToStatus(error);
      enqueued_finish_ = true;
      RunEnqueuedOperation();
    }

    void OnComplete() {
      enqueued_finish_ = true;
      RunEnqueuedOperation();
    }

    void TagOperationDone(bool success) {
      if (sent_final_request_) {
        // Nothing more to write.
        Shutdown();
      } else {
        if (success) {
          operation_in_progress_ = false;
          RunEnqueuedOperation();
        } else {
          // This happens when the runloop is shutting down.
          Shutdown();
        }
      }
    }

   private:
    void Shutdown() {
      // Kill the likely reference cycle. More at subscription_'s declaration.
      subscription_ = AnySubscription();
    }

    void RunEnqueuedOperation() {
      if (operation_in_progress_) {
        return;
      }
      if (next_response_) {
        operation_in_progress_ = true;
        stream_.Write(*next_response_, parent_.ToAlternateTag());
        next_response_.reset();
        subscription_.Request(ElementCount(1));
      } else if (enqueued_finish_ && !sent_final_request_) {
        enqueued_finish_ = false;
        operation_in_progress_ = true;
        sent_final_request_ = true;

        stream_.Finish(status_, parent_.ToAlternateTag());
      }
    }

    RsGrpcServerCall &parent_;
    // WARNING! subscription_ is likely to be part of a reference cycle that
    // goes like this:
    //
    // * RsGrpcServerCall has by-value ownership of
    // * RsGrpcServerCall::Writer, which (via subscription_) has a strong ref to
    // * The Subscription for RPC messages to the client, which is likely to
    //   have a strong reference to (depending on RPC handler code)
    // * The Subscription for RPC messages from the client, which has a strong
    //   reference to
    // * RsGrpcServerCall
    //
    // See Shutdown()
    //
    // Usually reference cycles should be seen as bugs and never be allowed to
    // happen. However, in this case, it's tricky to say which part of the
    // cycle should be eliminated.
    //
    // In a bidirectional streaming scenario, it is okay for either side of
    // the streams to end before the other. Because of this, it's not good to
    // make the reference that the RPC handler code has to the Subscription
    // for RPC messages from the client weak, because that would kill the whole
    // RPC as soon as the server was done writing, it could tear down the RPC.
    //
    // The same issue goes the other way if subscription_ would be made weak.
    //
    // Instead, this code allows the cycle to happen, but as soon as the stream
    // of messages to be sent to the client has finished, the subscription for
    // it is torn down, which terminates the reference cycle.
    //
    // This means that a server will leak memory if it has handlers that keep an
    // owning reference to the Subscription for messages from the client and
    // respond with streams that never complete.
    //
    // I think this is a reasonable compromise. What else could be done? How can
    // this library know that it is okay to tear down the RPC other than the
    // handling code completing its output stream?
    AnySubscription subscription_;
    std::unique_ptr<ResponseType> next_response_;
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
    auto call = RsGrpcTag::Ptr<RsGrpcServerCall>::TakeOver(
        new RsGrpcServerCall(
            error_handler, method, std::move(callback), service, cq));

    (service->*method)(
        &call->context_,
        &call->stream_,
        cq,
        cq,
        call->ToTag());
  }

  void OnNext(ResponseType &&response) {
    writer_.OnNext(std::move(response));
  }

  void OnError(std::exception_ptr &&error) {
    writer_.OnError(std::move(error));
  }

  void OnComplete() {
    writer_.OnComplete();
  }

  void Request(ElementCount count) {
    requested_ += count;
    MaybeReadNext();
  }

  void Cancel() {
    // TODO(peck): Handle cancellation
  }

 protected:
  void TagOperationDone(bool success) {
    switch (state_) {
      case State::INIT: {
        if (!success) {
          // The runloop was shut down before a request was received. There is
          // nothing to do here.
        } else {
          // Need to set _state before the call to init, in case it moves on to
          // the State::REQUESTED_DATA state immediately.
          state_ = State::WAITING_FOR_DATA_REQUEST;
          Init();
        }
        break;
      }
      case State::WAITING_FOR_DATA_REQUEST: {
        abort();  // Should not get here
        break;
      }
      case State::REQUESTED_DATA: {
        if (success) {
          subscriber_->OnNext(std::move(request_));
          state_ = State::WAITING_FOR_DATA_REQUEST;
          MaybeReadNext();
        } else {
          // The client has stopped sending requests.
          subscriber_->OnComplete();
          state_ = State::READ_STREAM_ENDED;
        }
        break;
      }
      case State::READ_STREAM_ENDED: {
        abort();  // Should not get here
        break;
      }
    }
  }

  void AlternateTagOperationDone(bool success) {
    writer_.TagOperationDone(success);
  }

 private:
  enum class State {
    INIT,
    WAITING_FOR_DATA_REQUEST,
    REQUESTED_DATA,
    READ_STREAM_ENDED
  };

  RsGrpcServerCall(
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
            this,
            &context_,
            &stream_) {}

  void Init() {
    auto response = callback_(
        CallContext(),
        AnyPublisher<RequestType>(MakePublisher(
          [self = ToShared(this)](auto &&subscriber) {
        if (self->subscriber_) {
          throw std::logic_error(
              "Can't subscribe to this Publisher more than once");
        }
        self->subscriber_.reset(
            new AnySubscriber<RequestType>(
                std::forward<decltype(subscriber)>(subscriber)));

        return MakeRsGrpcTagSubscription(self);
      })));

    static_assert(
        IsPublisher<decltype(response)>,
        "Callback return type must be Publisher");
    // TODO(peck): Is it right to have this as a weak pointer?
    writer_.Subscribed(AnySubscription(response.Subscribe(
        MakeRsGrpcTagSubscriber(ToWeak(this)))));

    Request(
        error_handler_,
        method_,
        std::move(callback_),  // Reuse the callback functor, don't copy
        &service_,
        &cq_);
  }

  void MaybeReadNext() {
    if (requested_ > 0 && state_ == State::WAITING_FOR_DATA_REQUEST) {
      --requested_;
      state_ = State::REQUESTED_DATA;
      stream_.Read(&request_, ToTag());
    }
  }

  // The number of elements that have been requested by the subscriber that have
  // not yet been requested to be read from gRPC.
  ElementCount requested_;

  std::unique_ptr<AnySubscriber<RequestType>> subscriber_;
  State state_ = State::INIT;
  GrpcErrorHandler error_handler_;
  Method method_;
  Callback callback_;
  Service &service_;
  grpc::ServerCompletionQueue &cq_;
  grpc::ServerContext context_;
  typename ServerCallTraits::Request request_;
  grpc::ServerAsyncReaderWriter<ResponseType, RequestType> stream_;
  Writer writer_;

  ResponseType response_;
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
class RsGrpcServerCallRequester : public InvocationRequester {
  using Service = typename ServerCallTraits::Service;

 public:
  RsGrpcServerCallRequester(
      Method method, Callback &&callback, Service *service)
      : method_(method), callback_(std::move(callback)), service_(*service) {}

  void RequestInvocation(
      GrpcErrorHandler error_handler,
      grpc::ServerCompletionQueue *cq) override {
    using ServerInvocation = RsGrpcServerCall<
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
    Shutdown(std::chrono::system_clock::now());
  }

  class Builder {
   public:
    template <typename Service>
    class ServiceBuilder {
     public:
      /**
       * The pointers passed to the constructor are not transformed by this
       * class; they need to stay alive for as long as this object exists.
       */
      ServiceBuilder(
          Service *service,
          std::vector<std::unique_ptr<detail::InvocationRequester>> *requesters)
          : service_(*service),
            invocation_requesters_(*requesters) {}

      // Unary RPC
      template <
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
                Callback>>(
                    method, std::forward<Callback>(callback));

        return *this;
      }

      // Server streaming
      template <
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
                Callback>>(
                    method, std::forward<Callback>(callback));

        return *this;
      }

      // Client streaming
      template <
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
                Callback>>(
                    method, std::forward<Callback>(callback));

        return *this;
      }

      // Bidi streaming
      template <
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
            detail::RsGrpcServerCallRequester<
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

  template <typename Stub>
  RsGrpcServiceClient<Stub> MakeClient(
      std::unique_ptr<Stub> &&stub) {
    return RsGrpcServiceClient<Stub>(std::move(stub), cq_.get());
  }

  /**
   * Block and process asynchronous events until the server is shut down.
   */
  void Run() {
    return detail::RsGrpcTag::ProcessAllEvents(cq_.get());
  }

  /**
   * Block and process one asynchronous event.
   *
   * Returns false if the event queue is shutting down.
   */
  bool Next() {
    return detail::RsGrpcTag::ProcessOneEvent(cq_.get());
  }

  /**
   * Block and process one asynchronous event, with a timeout.
   */
  template <typename T>
  grpc::CompletionQueue::NextStatus Next(const T& deadline) {
    return detail::RsGrpcTag::ProcessOneEvent(cq_.get(), deadline);
  }

  template <typename T>
  void Shutdown(const T &deadline) {
    // _server and _cq might be nullptr if this object has been moved out from.
    if (server_) {
      server_->Shutdown(deadline);
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

}  // namespace shk
