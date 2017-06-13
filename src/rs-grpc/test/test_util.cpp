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

#include "test_util.h"

#include <rs/concat.h>
#include <rs/just.h>

namespace shk {

Flatbuffer<TestRequest> MakeTestRequest(int data) {
  flatbuffers::grpc::MessageBuilder fbb;
  auto test_request = CreateTestRequest(fbb, data);
  fbb.Finish(test_request);
  return fbb.GetMessage<TestRequest>();
}

Flatbuffer<TestResponse> MakeTestResponse(int data) {
  flatbuffers::grpc::MessageBuilder fbb;
  auto test_response = CreateTestResponse(fbb, data);
  fbb.Finish(test_response);
  return fbb.GetMessage<TestResponse>();
}

void ShutdownAllowOutstandingCall(RsGrpcServer *server) {
  auto deadline = std::chrono::system_clock::now();
  server->Shutdown(deadline);
}

namespace {

template <typename Make>
auto MakeInfinite(const Make &make) -> Publisher<decltype(make(1))> {
  return Publisher<decltype(make(1))>(Concat(
      Just(make(1)),
      MakePublisher([make](auto &&subscriber) {
        return MakeInfinite(make).Subscribe(
            std::forward<decltype(subscriber)>(subscriber));
      })));
}

}  // anonymous namespace

Publisher<Flatbuffer<TestRequest>> MakeInfiniteRequest() {
  return MakeInfinite(&MakeTestRequest);
}

Publisher<Flatbuffer<TestResponse>> MakeInfiniteResponse() {
  return MakeInfinite(&MakeTestResponse);
}

}  // namespace shk
