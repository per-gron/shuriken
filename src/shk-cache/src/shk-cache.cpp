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

namespace shk {

struct FlatbufferRefTransform {
  template <typename T>
  std::shared_ptr<const T> operator()(flatbuffers::BufferRef<T> &&buffer) const {
    if (!buffer.Verify()) {
      // TODO(peck): Handle this more gracefully
      return nullptr;
    } else {
      SHK_ASSERT(buffer.must_free);
      buffer.must_free = false;
      uint8_t *buf = buffer.buf;
      return std::shared_ptr<const T>(
          buffer.GetRoot(),
          [buf](const T *) { free(buf); });
    }
  }
};

// Track the server instance, so we can terminate it later.
bool server_ready = false;
// Mutex to protec this variable.
std::mutex wait_for_server;
std::condition_variable server_instance_cv;

// This function implements the server thread.
void runServer() {
  auto server_address = "0.0.0.0:50051";

  ShkCache::Config::AsyncService service;
  grpc::ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  auto cq = builder.AddCompletionQueue();
  auto server = builder.BuildAndStart();

  {
    std::lock_guard<std::mutex> lock(wait_for_server);
    server_ready = true;
  }
  server_instance_cv.notify_one();

  grpc::ServerContext context;
  flatbuffers::BufferRef<ShkCache::ConfigGetRequest> request;
  grpc::ServerAsyncResponseWriter<
      flatbuffers::BufferRef<ShkCache::ConfigGetResponse>> responder(&context);

  service.RequestGet(
      &context, &request, &responder, cq.get(), cq.get(), (void *)1);

  flatbuffers::BufferRef<ShkCache::ConfigGetResponse> response;
  flatbuffers::FlatBufferBuilder response_builder;
  {
    auto store_config = ShkCache::CreateStoreConfig(
        response_builder,
        /*soft_store_entry_size_limit:*/1,
        /*hard_store_entry_size_limit:*/2);
    auto config_get_response = ShkCache::CreateConfigGetResponse(
        response_builder,
        store_config);

    response_builder.Finish(config_get_response);

    // Since we keep reusing the same FlatBufferBuilder, the memory it owns
    // remains valid until the next call (this BufferRef doesn't own the
    // memory it points to).
    response = flatbuffers::BufferRef<ShkCache::ConfigGetResponse>(
        response_builder.GetBufferPointer(),
        response_builder.GetSize());
  }

  {
    grpc::Status status;
    void *got_tag;
    bool ok = false;
    cq->Next(&got_tag, &ok);
    if (ok && got_tag == (void *)1) {
      responder.Finish(response, status, (void *)2);
    } else {
      std::cout << "[SERVER] FAIL!" << std::endl;
    }
  }

  {
    void *got_tag;
    bool ok = false;
    cq->Next(&got_tag, &ok);
    if (ok && got_tag == (void *)2) {
      // clean up
    } else {
      std::cout << "[SERVER] CLEANUP FAIL!" << std::endl;
    }
  }

  server->Shutdown();

  cq->Shutdown();
  {
    void *got_tag;
    bool ok = false;
    if (cq->Next(&got_tag, &ok) != false) {
      std::cout << "[SERVER] FAILED TO SHUT DOWN" << std::endl;
    }
  }
}

int main(int /*argc*/, const char * /*argv*/[]) {
  // Launch server.
  std::thread server_thread(runServer);

  // wait for server to spin up.
  {
    std::unique_lock<std::mutex> lock(wait_for_server);
    while (!server_ready) {
      server_instance_cv.wait(lock);
    }
  }

  // Now connect the client.
  auto channel = grpc::CreateChannel(
      "localhost:50051",
      grpc::InsecureChannelCredentials());

  flatbuffers::FlatBufferBuilder fbb;
  {
    auto config_get_request = ShkCache::CreateConfigGetRequest(fbb);
    fbb.Finish(config_get_request);
    auto request = flatbuffers::BufferRef<ShkCache::ConfigGetRequest>(
        fbb.GetBufferPointer(), fbb.GetSize());

    RxGrpcClientHandler handler;

    auto client = handler.makeClient<FlatbufferRefTransform>(
        ShkCache::Config::NewStub(channel));

    client
        .invoke(&ShkCache::Config::Stub::AsyncGet, request)
        .subscribe(
            [](const std::shared_ptr<
                   const ShkCache::ConfigGetResponse> &response) {
              if (!response) {
                std::cout << "Verification failed!" << std::endl;
              } else {
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
              }
            },
            []() {
              std::cout << "OnCompleted" << std::endl;
            });

    handler.run();
  }

  server_thread.join();

  channel.reset();

  return 0;
}

}  // namespace shk

int main(int argc, const char *argv[]) {
  return shk::main(argc, argv);
}
