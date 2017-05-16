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

#include <rxcpp/rx-observable.hpp>

namespace shk {
namespace detail {

struct RxGrpcIdentityTransform {
  template <typename T>
  T operator()(T &&value) const {
    return std::forward<T>(value);
  }
};

class RxGrpcTag {
 public:
  enum class Response {
    OK,
    DELETE_ME
  };

  virtual ~RxGrpcTag() = default;

  virtual Response operator()() = 0;
};

template <typename ResponseType, typename ResponseTransform>
class RxGrpcClientInvocation : public RxGrpcTag {
 public:
  using TransformedResponseType = decltype(
      std::declval<ResponseTransform>()(std::declval<ResponseType>()));

  RxGrpcClientInvocation(rxcpp::subscriber<TransformedResponseType> &&subscriber)
      : _subscriber(std::move(subscriber)) {}

  Response operator()() override {
    _subscriber.on_next(_transform(std::move(_response)));
    _subscriber.on_completed();

    return Response::DELETE_ME;
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
  ResponseTransform _transform;
  rxcpp::subscriber<TransformedResponseType> _subscriber;
  ResponseType _response;
  grpc::ClientContext _context;
  grpc::Status _status;
};

}  // namespace detail

template <typename Stub, typename ResponseTransform>
class RxGrpcServiceClient {
 public:
  RxGrpcServiceClient(std::unique_ptr<Stub> &&stub, grpc::CompletionQueue *cq)
      : _stub(std::move(stub)), _cq(*cq) {}

  template <typename ResponseType, typename RequestType>
  rxcpp::observable<
      typename detail::RxGrpcClientInvocation<
          ResponseType, ResponseTransform>::TransformedResponseType>
  invoke(
      std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>>
      (Stub::*invoke)(
          grpc::ClientContext *context,
          const RequestType &request,
          grpc::CompletionQueue *cq),
      const RequestType &request,
      grpc::ClientContext &&context = grpc::ClientContext()) {

    using ClientCall =
        detail::RxGrpcClientInvocation<ResponseType, ResponseTransform>;
    using TransformedResponseType =
        typename ClientCall::TransformedResponseType;

    return rxcpp::observable<>::create<TransformedResponseType>([&](
        rxcpp::subscriber<TransformedResponseType> subscriber) {

      auto call = std::unique_ptr<ClientCall>(
          new ClientCall(std::move(subscriber)));
      auto rpc = ((_stub.get())->*invoke)(&call->context(), request, &_cq);
      rpc->Finish(&call->response(), &call->status(), call.release());
    });
  }

 private:
  std::unique_ptr<Stub> _stub;
  grpc::CompletionQueue &_cq;
};

class RxGrpcServerHandler {
 public:

 private:
};

class RxGrpcClientHandler {
 public:
  void run() {
    void *got_tag;
    bool ok = false;
    _cq.Next(&got_tag, &ok);  // TODO(peck): Use return value here

    if (ok) {
      detail::RxGrpcTag *tag = reinterpret_cast<detail::RxGrpcTag *>(got_tag);
      if ((*tag)() == detail::RxGrpcTag::Response::DELETE_ME) {
        delete tag;
      }
    } else {
      std::cout << "Request not ok" << std::endl;
    }
  }

  template <
      typename ResponseTransform = detail::RxGrpcIdentityTransform,
      typename Stub>
  RxGrpcServiceClient<Stub, ResponseTransform> makeClient(
      std::unique_ptr<Stub> &&stub) {
    return RxGrpcServiceClient<Stub, ResponseTransform>(std::move(stub), &_cq);
  }

 private:
  grpc::CompletionQueue _cq;
};

}  // namespace shk
