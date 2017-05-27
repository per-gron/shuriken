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

#include <stdio.h>
#include <thread>

#include <grpc++/grpc++.h>

#include <shk-cache/shkcache_generated.h>
#include <shk-cache/shkcache.grpc.fb.h>
#include <util/assert.h>

#include "rx_grpc.h"
#include "rx_grpc_flatbuffers.h"

namespace shk {

auto configGet(const Flatbuffer<ShkCache::ConfigGetRequest> &request) {
  flatbuffers::FlatBufferBuilder response_builder;
  auto store_config = ShkCache::CreateStoreConfig(
      response_builder,
      /*soft_store_entry_size_limit:*/1,
      /*hard_store_entry_size_limit:*/2);
  auto config_get_response = ShkCache::CreateConfigGetResponse(
      response_builder,
      store_config);

  response_builder.Finish(config_get_response);

  auto response = Flatbuffer<ShkCache::ConfigGetResponse>::fromBuilder(
      &response_builder);

  return rxcpp::observable<>::just(response);
}

RxGrpcServer makeServer() {
  auto server_address = "0.0.0.0:50051";

  RxGrpcServer::Builder builder;
  builder.grpcServerBuilder()
      .AddListeningPort(server_address, grpc::InsecureServerCredentials());

  builder.registerService<ShkCache::Config::AsyncService>()
      .registerMethod<FlatbufferRefTransform>(
          &ShkCache::Config::AsyncService::RequestGet,
          &configGet);

  return builder.buildAndStart();
}

auto makeConfigGetRequest() {
  flatbuffers::FlatBufferBuilder fbb;
  auto config_get_request = ShkCache::CreateConfigGetRequest(fbb);
  fbb.Finish(config_get_request);
  return Flatbuffer<ShkCache::ConfigGetRequest>::fromBuilder(
      &fbb);
}

int main(int /*argc*/, const char * /*argv*/[]) {
  // TODO(peck): Try to reduce copying of messages
  // TODO(peck): Add support for cancellation (cancel is called unsubscribe)
  // TODO(peck): Add support for timeouts
  // TODO(peck): Add support for backpressure (streaming output requires only
  //     one outstanding request at a time. Not possible atm.)
  // TODO(peck): Test
  //  * handlers that return observables that fail with an error
  //  * ill-formed flatbuffers
  //  * handlers that return too early
  //  * finishing bidi and unidirectional streams in different orders
  //  * go through the code and look for stuff
  //  * what happens if writesdone is not called? Does the server stall then?

  auto server = makeServer();
  std::thread server_thread([&] { server.run(); });

  auto channel = grpc::CreateChannel(
      "localhost:50051",
      grpc::InsecureChannelCredentials());

  RxGrpcClient client;  // TODO(peck): Rename to ClientFactory or Runloop

  auto config_client = client.makeClient<FlatbufferRefTransform>(
      ShkCache::Config::NewStub(channel));

  int requests_left = 1;
  auto request_done = [&]() {
    if (--requests_left == 0) {
      client.shutdown();
    }
  };
  for (int i = 0; i < requests_left; i++) {
    config_client
        .invoke(
            &ShkCache::Config::Stub::AsyncGet, makeConfigGetRequest())
        .subscribe(
            [](Flatbuffer<ShkCache::ConfigGetResponse> response) {
              if (auto config = response->config()) {
                std::cout <<
                    "RPC response: " <<
                    config->soft_store_entry_size_limit() <<
                    ", " <<
                    config->hard_store_entry_size_limit() <<
                    std::endl;
              } else {
                std::cout << "RPC response: [no config]" << std::endl;
              }
            },
            [&request_done](std::exception_ptr error) {
              // TODO(peck): Do something useful with this
              std::cout << "OnError" << std::endl;
              request_done();
            },
            [&request_done]() {
              std::cout << "OnCompleted" << std::endl;
              request_done();
            });
  }

  client.run();

  server.shutdown();
  server_thread.join();

  return 0;
}

}  // namespace shk

int main(int argc, const char *argv[]) {
  return shk::main(argc, argv);
}
