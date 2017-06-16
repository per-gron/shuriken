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

#include <rs/publisher.h>
#include <rs/subscriber.h>
#include <rs/subscription.h>
#include <rs/throw.h>
#include <rs-grpc/detail/rs_grpc_tag.h>
#include <rs-grpc/detail/subscriber.h>
#include <rs-grpc/detail/subscription.h>
#include <rs-grpc/grpc_error.h>

namespace shk {
namespace detail {

template <
    typename ResponseType,
    typename SubscriberType>
void HandleUnaryResponse(
    bool success,
    const grpc::Status &status,
    ResponseType &&response,
    SubscriberType *subscriber) {
  if (!success) {
    // The runloop is shutting down. This is not an error condition, but it
    // means that no more signals will be sent to the subscription.
  } else if (status.ok()) {
    subscriber->OnNext(std::forward<ResponseType>(response));
    subscriber->OnComplete();
  } else {
    subscriber->OnError(std::make_exception_ptr(GrpcError(status)));
  }
}

template <
    typename Reader,
    typename ResponseType,
    typename RequestType>
class RsGrpcClientCall;

/**
 * Unary client RPC.
 */
template <
    typename ResponseType,
    typename RequestType>
class RsGrpcClientCall<
    grpc::ClientAsyncResponseReader<ResponseType>,
    ResponseType,
    RequestType> : public RsGrpcTag, SubscriptionBase {
 public:
  RsGrpcClientCall(
      const RequestType &request,
      Subscriber<ResponseType> &&subscriber)
      : request_(request),
        subscriber_(std::move(subscriber)) {}

  template <typename Stub>
  auto Invoke(
      std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>>
      (Stub::*invoke)(
          grpc::ClientContext *context,
          const RequestType &request,
          grpc::CompletionQueue *cq),
      Stub *stub,
      grpc::CompletionQueue *cq) {
    invoke_ = [this, stub, invoke, cq] {
      return (stub->*invoke)(&context_, request_, cq);
    };
    return MakeRsGrpcTagSubscription(ToShared(this));
  }

  void Request(ElementCount count) {
    if (invoke_) {
      if (cancelled_) {
        invoke_ = nullptr;
      } else if (count > 0) {
        auto stream = invoke_();
        stream->Finish(&response_, &status_, ToTag());
        invoke_ = nullptr;
      }
    }
  }

  void Cancel() {
    cancelled_ = true;
    context_.TryCancel();
  }

 protected:
  void TagOperationDone(bool success) override {
    if (!cancelled_) {
      HandleUnaryResponse(
          success, status_, std::move(response_), &subscriber_);
    }
  }

 private:
  // invoke_ is set on the initial call to Invoke and unset when the actual
  // request has been made.
  std::function<std::unique_ptr<
      grpc::ClientAsyncResponseReader<ResponseType>> ()> invoke_;
  bool cancelled_ = false;
  static_assert(
      !std::is_reference<RequestType>::value,
      "Request type must be held by value");
  RequestType request_;
  grpc::ClientContext context_;
  ResponseType response_;
  Subscriber<ResponseType> subscriber_;
  grpc::Status status_;
};

/**
 * Server streaming.
 */
template <
    typename ResponseType,
    typename RequestType>
class RsGrpcClientCall<
    grpc::ClientAsyncReader<ResponseType>,
    ResponseType,
    RequestType> : public RsGrpcTag, public SubscriptionBase {
 public:
  RsGrpcClientCall(
      const RequestType &request,
      Subscriber<ResponseType> &&subscriber)
      : request_(request),
        subscriber_(std::move(subscriber)) {}

  template <typename Stub>
  auto Invoke(
      std::unique_ptr<grpc::ClientAsyncReader<ResponseType>>
      (Stub::*invoke)(
          grpc::ClientContext *context,
          const RequestType &request,
          grpc::CompletionQueue *cq,
          void *tag),
      Stub *stub,
      grpc::CompletionQueue *cq) {
    invoke_ = [this, stub, invoke, cq] {
      return (stub->*invoke)(&context_, request_, cq, ToTag());
    };
    return MakeRsGrpcTagSubscription(ToShared(this));
  }

  void Request(ElementCount count) {
    if (cancelled_) {
      // Nothing to do
    } else if (invoke_) {
      // The initial call to invoke has not yet been made
      if (count > 0) {
        requested_ = count;
        stream_ = invoke_();
        invoke_ = nullptr;
      }
    } else {
      requested_ += count;
      if (state_ == State::AWAITING_REQUEST) {
        MaybeReadNext();
      }
    }
  }

  void Cancel() {
    cancelled_ = true;
    context_.TryCancel();
  }

 protected:
  void TagOperationDone(bool success) override {
    switch (state_) {
      case State::INIT: {
        MaybeReadNext();
        break;
      }
      case State::AWAITING_REQUEST: {
        // This is an internal error: When awaiting request there should be no
        // outstanding gRPC request.
        throw std::logic_error("Should not get response when awaiting request");
        break;
      }
      case State::READING_RESPONSE: {
        if (!success) {
          // We have reached the end of the stream.
          state_ = State::FINISHING;
          stream_->Finish(&status_, ToTag());
        } else {
          subscriber_.OnNext(std::move(response_));
          MaybeReadNext();
        }
        break;
      }
      case State::FINISHING: {
        if (status_.ok()) {
          subscriber_.OnComplete();
        } else {
          subscriber_.OnError(std::make_exception_ptr(GrpcError(status_)));
        }
        break;
      }
      case State::READ_FAILURE: {
        break;
      }
    }
  }

 private:
  enum class State {
    INIT,
    AWAITING_REQUEST,  // Awaiting Request call to the Subscription
    READING_RESPONSE,
    FINISHING,
    READ_FAILURE
  };

  void MaybeReadNext() {
    if (requested_ > 0) {
      --requested_;
      state_ = State::READING_RESPONSE;
      // We are now handing over ourselves to gRPC. If the subscriber gets rid
      // of the Subscription we must still make sure to stay alive until gRPC
      // calls us back with a response, so here we start owning ourselves.
      stream_->Read(&response_, ToTag());
    } else {
      // Because this object is not given to a gRPC CompletionQueue, the object
      // will leak if the subscriber gets rid of its Subscription without
      // requesting more elements (which it is perfectly allowed to do).
      state_ = State::AWAITING_REQUEST;
    }
  }

  // The number of elements that have been requested by the subscriber that have
  // not yet been requested to be read from gRPC.
  ElementCount requested_;
  static_assert(
      !std::is_reference<RequestType>::value,
      "Request type must be held by value");
  RequestType request_;
  grpc::ClientContext context_;

  State state_ = State::INIT;
  ResponseType response_;
  Subscriber<ResponseType> subscriber_;
  grpc::Status status_;
  std::unique_ptr<grpc::ClientAsyncReader<ResponseType>> stream_;

  // Member variables that are stored in Invoke for use by Request. Set only
  // between the Invoke call and the first Request call that requests non-zero
  // elements.
  std::function<
      std::unique_ptr<grpc::ClientAsyncReader<ResponseType>> ()> invoke_;
  bool cancelled_ = false;
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
    typename ResponseType>
class RsGrpcClientCall<
    grpc::ClientAsyncWriter<RequestType>,
    ResponseType,
    RequestType> :
        public RsGrpcTag, public SubscriberBase, public SubscriptionBase {
 public:
  RsGrpcClientCall(
      const Publisher<RequestType> &requests,
      Subscriber<ResponseType> &&subscriber)
      : requests_(requests),
        subscriber_(std::move(subscriber)) {}

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
    invoke_ = [this, stub, invoke, cq] {
      return (stub->*invoke)(&context_, &response_, cq, ToTag());
    };
    return MakeRsGrpcTagSubscription(ToShared(this));
  }

  void Request(ElementCount count) {
    if (cancelled_) {
      return;
    }

    if (!stream_ && count > 0) {
      operation_in_progress_ = true;
      stream_ = invoke_();
      subscription_ = Subscription(requests_.Subscribe(
          MakeRsGrpcTagSubscriber(ToWeak(this))));
      subscription_.Request(ElementCount(1));
    }
  }

  void Cancel() {
    cancelled_ = true;
    context_.TryCancel();
    subscription_.Cancel();
  }

  void OnNext(RequestType &&request) {
    if (next_request_) {
      OnError(std::make_exception_ptr(
          std::logic_error("Backpressure violation")));
    } else {
      next_request_ = std::make_unique<RequestType>(std::move(request));
      RunEnqueuedOperation();
    }
  }

  void OnError(std::exception_ptr &&error) {
    // This triggers RunEnqueuedOperation to Finish the stream.
    request_stream_error_ = error;
    enqueued_writes_done_ = true;
    RunEnqueuedOperation();
  }

  void OnComplete() {
    enqueued_writes_done_ = true;
    RunEnqueuedOperation();
  }

 protected:
  void TagOperationDone(bool success) override {
    if (sent_final_request_) {
      if (cancelled_) {
        // Do nothing
      } else if (request_stream_error_) {
        subscriber_.OnError(std::move(request_stream_error_));
      } else {
        HandleUnaryResponse(
            success, status_, std::move(response_), &subscriber_);
      }
    } else {
      if (success) {
        operation_in_progress_ = false;
        RunEnqueuedOperation();
      } else {
        // This happens when the runloop is shutting down.
        if (!cancelled_) {
          HandleUnaryResponse(
              success, status_, std::move(response_), &subscriber_);
        }
      }
    }
  }

 private:
  void RunEnqueuedOperation() {
    if (operation_in_progress_ || cancelled_) {
      return;
    }
    if (next_request_) {
      operation_in_progress_ = true;
      stream_->Write(*next_request_, ToTag());
      next_request_.reset();
      subscription_.Request(ElementCount(1));
    } else if (enqueued_writes_done_) {
      enqueued_writes_done_ = false;
      enqueued_finish_ = true;
      operation_in_progress_ = true;
      stream_->WritesDone(ToTag());
    } else if (enqueued_finish_) {
      enqueued_finish_ = false;
      operation_in_progress_ = true;
      sent_final_request_ = true;

      stream_->Finish(&status_, ToTag());
    }
  }

  bool operation_in_progress_ = false;
  bool cancelled_ = false;
  Publisher<RequestType> requests_;
  ResponseType response_;
  std::unique_ptr<grpc::ClientAsyncWriter<RequestType>> stream_;
  std::function<
      std::unique_ptr<grpc::ClientAsyncWriter<RequestType>> ()> invoke_;
  grpc::ClientContext context_;
  Subscriber<ResponseType> subscriber_;
  Subscription subscription_;

  std::exception_ptr request_stream_error_;
  bool sent_final_request_ = false;

  std::unique_ptr<RequestType> next_request_;
  bool enqueued_writes_done_ = false;
  bool enqueued_finish_ = false;
  grpc::Status status_;
};

/**
 * Bidi.
 */
template <
    typename RequestType,
    typename ResponseType>
class RsGrpcClientCall<
    grpc::ClientAsyncReaderWriter<RequestType, ResponseType>,
    ResponseType,
    RequestType>
        : public RsGrpcTag, public SubscriberBase, public SubscriptionBase {
 private:
  /**
   * The purpose of this class is to encapsulate the logic for reading from
   * the rest of this RsGrpcClientCall template specialization.
   */
  class Reader {
   public:
    Reader(
        RsGrpcClientCall *parent,
        const std::function<void ()> &shutdown,
        Subscriber<ResponseType> &&subscriber)
        : parent_(*parent),
          shutdown_(shutdown),
          subscriber_(std::move(subscriber)) {}

    void Invoke(
        grpc::ClientAsyncReaderWriter<RequestType, ResponseType> *stream) {
      stream_ = stream;
    }

    void Request(ElementCount count) {
      requested_ += count;
      if (state_ == State::AWAITING_REQUEST) {
        if (requested_ > 0) {
          --requested_;
          state_ = State::READING_RESPONSE;
          stream_->Read(&response_, parent_.ToAlternateTag());
        }
      }
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
    void Finish(bool cancelled, const grpc::Status &status) {
      if (!cancelled) {
        if (!status.ok()) {
          subscriber_.OnError(std::make_exception_ptr(GrpcError(status)));
        } else if (error_) {
          subscriber_.OnError(std::move(error_));
        } else {
          subscriber_.OnComplete();
        }
      }
    }

    void TagOperationDone(bool success) {
      if (!success || error_) {
        // We have reached the end of the stream.
        state_ = State::END;
        shutdown_();
      } else {
        subscriber_.OnNext(std::move(response_));
        state_ = State::AWAITING_REQUEST;
        Request(ElementCount(0));
      }
    }

   private:
    enum class State {
      AWAITING_REQUEST,  // Awaiting Request call to the Subscription
      READING_RESPONSE,
      END
    };

    RsGrpcClientCall &parent_;
    State state_ = State::AWAITING_REQUEST;
    // The number of elements that have been requested by the subscriber that
    // have not yet been requested to be read from gRPC.
    ElementCount requested_;

    std::exception_ptr error_;
    // Should be called when the read stream is finished. Care must be taken so
    // that this is not called when there is an outstanding async operation.
    std::function<void ()> shutdown_;
    grpc::ClientAsyncReaderWriter<RequestType, ResponseType> *stream_ = nullptr;
    Subscriber<ResponseType> subscriber_;
    ResponseType response_;
  };

 public:
  RsGrpcClientCall(
      const Publisher<RequestType> &requests,
      Subscriber<ResponseType> &&subscriber)
      : reader_(
            this,
            [this] { reader_done_ = true; TryShutdown(); },
            std::move(subscriber)),
        requests_(requests) {}

  template <typename Stub>
  auto Invoke(
      std::unique_ptr<grpc::ClientAsyncReaderWriter<RequestType, ResponseType>>
      (Stub::*invoke)(
          grpc::ClientContext *context,
          grpc::CompletionQueue *cq,
          void *tag),
      Stub *stub,
      grpc::CompletionQueue *cq) {
    invoke_ = [this, stub, invoke, cq] {
      return (stub->*invoke)(&context_, cq, ToTag());
    };
    return MakeRsGrpcTagSubscription(ToShared(this));
  }

  void OnNext(RequestType &&request) {
    if (next_request_) {
      OnError(std::make_exception_ptr(
          std::logic_error("Backpressure violation")));
    } else {
      next_request_ = std::make_unique<RequestType>(std::move(request));
      RunEnqueuedOperation();
    }
  }

  void OnError(std::exception_ptr &&error) {
    reader_.OnError(error);
    enqueued_writes_done_ = true;
    RunEnqueuedOperation();
  }

  void OnComplete() {
    enqueued_writes_done_ = true;
    RunEnqueuedOperation();
  }

  void Request(ElementCount count) {
    if (cancelled_) {
      // Do nothing
    } else if (invoke_) {
      // The initial call to invoke has not yet been made
      if (count > 0) {
        operation_in_progress_ = true;
        stream_ = invoke_();
        invoke_ = nullptr;
        reader_.Invoke(stream_.get());
        reader_.Request(count);

        subscription_ = Subscription(requests_.Subscribe(
            MakeRsGrpcTagSubscriber(ToWeak(this))));
        subscription_.Request(ElementCount(1));
      }
    } else {
      reader_.Request(count);
    }
  }

  void Cancel() {
    cancelled_ = true;
    context_.TryCancel();
    subscription_.Cancel();
  }

 protected:
  void TagOperationDone(bool success) override {
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
        cancelled_ = true;
        TryShutdown();
      }
    }
  }

  void AlternateTagOperationDone(bool success) override {
    reader_.TagOperationDone(success);
  }

 private:
  enum class State {
    SENDING_REQUESTS,
    TO_SEND_WRITES_DONE,
    TO_SEND_FINISH,
    DONE
  };

  void RunEnqueuedOperation() {
    if (operation_in_progress_ || cancelled_) {
      return;
    }
    if (next_request_) {
      operation_in_progress_ = true;
      stream_->Write(*next_request_, ToTag());
      next_request_.reset();
      subscription_.Request(ElementCount(1));
    } else if (enqueued_writes_done_) {
      enqueued_writes_done_ = false;
      enqueued_finish_ = true;
      operation_in_progress_ = true;
      stream_->WritesDone(ToTag());
    } else if (enqueued_finish_) {
      enqueued_finish_ = false;
      operation_in_progress_ = true;
      sent_final_request_ = true;

      stream_->Finish(&status_, ToTag());
    }
  }

  void TryShutdown() {
    if (writer_done_ && reader_done_) {
      reader_.Finish(cancelled_, status_);
    }
  }

  bool operation_in_progress_ = false;
  // Set only between the Invoke call and the first Request call that requests
  // non-zero elements.
  std::function<std::unique_ptr<
      grpc::ClientAsyncReaderWriter<RequestType, ResponseType>> ()> invoke_;
  bool cancelled_ = false;
  Reader reader_;
  bool reader_done_ = false;

  Publisher<RequestType> requests_;
  ResponseType response_;
  std::unique_ptr<
      grpc::ClientAsyncReaderWriter<RequestType, ResponseType>> stream_;
  grpc::ClientContext context_;
  Subscription subscription_;

  bool sent_final_request_ = false;
  bool writer_done_ = false;

  std::unique_ptr<RequestType> next_request_;
  bool enqueued_writes_done_ = false;
  bool enqueued_finish_ = false;
  grpc::Status status_;
};

}  // namespace detail

template <typename Stub>
class RsGrpcServiceClient {
 public:
  RsGrpcServiceClient(std::unique_ptr<Stub> &&stub, grpc::CompletionQueue *cq)
      : stub_(std::move(stub)), cq_(*cq) {}

  /**
   * Unary rpc.
   */
  template <typename ResponseType, typename RequestType>
  auto Invoke(
      std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>>
      (Stub::*invoke)(
          grpc::ClientContext *context,
          const RequestType &request,
          grpc::CompletionQueue *cq),
      const RequestType &request,
      grpc::ClientContext &&context = grpc::ClientContext()) {
    return InvokeImpl<
        grpc::ClientAsyncResponseReader<ResponseType>,
        ResponseType,
        RequestType>(
            invoke,
            request,
            std::move(context));
  }

  /**
   * Server streaming.
   */
  template <typename ResponseType, typename RequestType>
  auto Invoke(
      std::unique_ptr<grpc::ClientAsyncReader<ResponseType>>
      (Stub::*invoke)(
          grpc::ClientContext *context,
          const RequestType &request,
          grpc::CompletionQueue *cq,
          void *tag),
      const RequestType &request,
      grpc::ClientContext &&context = grpc::ClientContext()) {
    return InvokeImpl<
        grpc::ClientAsyncReader<ResponseType>,
        ResponseType,
        RequestType>(
            invoke,
            request,
            std::move(context));
  }

  /**
   * Client streaming.
   */
  template <typename RequestType, typename ResponseType, typename PublisherType>
  auto Invoke(
      std::unique_ptr<grpc::ClientAsyncWriter<RequestType>>
      (Stub::*invoke)(
          grpc::ClientContext *context,
          ResponseType *response,
          grpc::CompletionQueue *cq,
          void *tag),
      PublisherType &&requests,
      grpc::ClientContext &&context = grpc::ClientContext()) {
    return InvokeImpl<
        grpc::ClientAsyncWriter<RequestType>,
        ResponseType,
        RequestType>(
            invoke,
            Publisher<RequestType>(std::forward<PublisherType>(requests)),
            std::move(context));
  }

  /**
   * Bidi streaming
   */
  template <typename RequestType, typename ResponseType, typename PublisherType>
  auto Invoke(
      std::unique_ptr<grpc::ClientAsyncReaderWriter<RequestType, ResponseType>>
      (Stub::*invoke)(
          grpc::ClientContext *context,
          grpc::CompletionQueue *cq,
          void *tag),
      PublisherType &&requests,
      grpc::ClientContext &&context = grpc::ClientContext()) {
    return InvokeImpl<
        grpc::ClientAsyncReaderWriter<RequestType, ResponseType>,
        ResponseType,
        RequestType>(
            invoke,
            Publisher<RequestType>(std::forward<PublisherType>(requests)),
            std::move(context));
  }

 private:
  template <
      typename Reader,
      typename ResponseType,
      typename RequestType,
      typename RequestOrPublisher,
      typename Invoke>
  auto InvokeImpl(
      Invoke invoke,
      RequestOrPublisher &&request_or_publisher,
      grpc::ClientContext &&context = grpc::ClientContext()) {

    return MakePublisher([
        this,
        request_or_publisher =
            std::forward<RequestOrPublisher>(request_or_publisher),
        invoke](auto &&subscriber) {
      using ClientInvocation =
          detail::RsGrpcClientCall<
              Reader,
              ResponseType,
              RequestType>;

      auto call = detail::RsGrpcTag::Ptr<ClientInvocation>::TakeOver(
          new ClientInvocation(
              request_or_publisher,
              Subscriber<ResponseType>(
                  std::forward<decltype(subscriber)>(subscriber))));
      return call->Invoke(invoke, stub_.get(), &cq_);
    });
  }

  std::unique_ptr<Stub> stub_;
  grpc::CompletionQueue &cq_;
};

class RsGrpcClient {
 public:
  ~RsGrpcClient() {
    Shutdown();
  }

  template <typename Stub>
  RsGrpcServiceClient<Stub> MakeClient(std::unique_ptr<Stub> &&stub) {
    return RsGrpcServiceClient<Stub>(std::move(stub), &cq_);
  }

  /**
   * Block and process asynchronous events until the server is shut down.
   */
  void Run() {
    return detail::RsGrpcTag::ProcessAllEvents(&cq_);
  }

  /**
   * Block and process one asynchronous event.
   *
   * Returns false if the event queue is shutting down.
   */
  bool Next() {
    return detail::RsGrpcTag::ProcessOneEvent(&cq_);
  }

  /**
   * Block and process one asynchronous event, with a timeout.
   */
  template <typename T>
  grpc::CompletionQueue::NextStatus Next(const T& deadline) {
    return detail::RsGrpcTag::ProcessOneEvent(&cq_, deadline);
  }

  void Shutdown() {
    cq_.Shutdown();
  }

 private:
  grpc::CompletionQueue cq_;
};

}  // namespace shk
