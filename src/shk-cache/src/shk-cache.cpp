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

using namespace ShkCache;

namespace shk {

struct FlatbufferRefTransform {
  template <typename T>
  std::shared_ptr<const T> operator()(flatbuffers::BufferRef<T> &&buffer) const {
    if (!buffer.Verify()) {
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

class ConfigService final : public ShkCache::Config::Service {
  grpc::Status Get(
      grpc::ServerContext* context,
      const flatbuffers::BufferRef<ConfigGetRequest>* request,
      flatbuffers::BufferRef<ConfigGetResponse>* response) override {
    // Create a response from the incoming request name.
    _builder.Clear();

    auto store_config = CreateStoreConfig(
        _builder,
        /*soft_store_entry_size_limit:*/1,
        /*hard_store_entry_size_limit:*/2);
    auto config_get_response = CreateConfigGetResponse(
        _builder,
        store_config);

    _builder.Finish(config_get_response);

    // Since we keep reusing the same FlatBufferBuilder, the memory it owns
    // remains valid until the next call (this BufferRef doesn't own the
    // memory it points to).
    *response = flatbuffers::BufferRef<ConfigGetResponse>(
        _builder.GetBufferPointer(),
        _builder.GetSize());
    return grpc::Status::OK;
  }

 private:
  flatbuffers::FlatBufferBuilder _builder;
};

// Track the server instance, so we can terminate it later.
grpc::Server *server_instance = nullptr;
// Mutex to protec this variable.
std::mutex wait_for_server;
std::condition_variable server_instance_cv;

// This function implements the server thread.
void RunServer() {
  auto server_address = "0.0.0.0:50051";

  ConfigService config_service;
  grpc::ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&config_service);

  // Start the server. Lock to change the variable we're changing.
  wait_for_server.lock();
  server_instance = builder.BuildAndStart().release();
  wait_for_server.unlock();
  server_instance_cv.notify_one();

  std::cout << "Server listening on " << server_address << std::endl;
  // This will block the thread and serve requests.
  server_instance->Wait();
}

int main(int /*argc*/, const char * /*argv*/[]) {
  // Launch server.
  std::thread server_thread(RunServer);

  // wait for server to spin up.
  std::unique_lock<std::mutex> lock(wait_for_server);
  while (!server_instance) {
    server_instance_cv.wait(lock);
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

    RxGrpcHandler handler;

    auto client = handler.makeClient<FlatbufferRefTransform>(
        ShkCache::Config::NewStub(channel));

    client
        .invoke(&ShkCache::Config::Stub::AsyncGet, request)
        .subscribe(
            [](const std::shared_ptr<const ConfigGetResponse> &response) {
              if (!response) {
                std::cout << "Verification failed!" << std::endl;
              } else {
                if (auto config = response->config()) {
                  std::cout <<
                      "RPC response: " << config->soft_store_entry_size_limit() <<
                      ", " << config->hard_store_entry_size_limit() << std::endl;
                } else {
                  std::cout << "RPC response: [no config]" << std::endl;
                }
              }
            },
            []() {
              printf("OnCompleted\n");
            });

    handler.run();
  }

  server_instance->Shutdown();

  server_thread.join();

  delete server_instance;

  return 0;
}

}  // namespace shk

int main(int argc, const char *argv[]) {
  return shk::main(argc, argv);
}
