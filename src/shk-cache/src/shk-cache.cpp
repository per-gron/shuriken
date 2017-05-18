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

/**
 * A Flatbuffer is an owning typed pointer to a valid Flatbuffer buffer. It is
 * illegal to construct a Flatbuffer with memory that is not a valid flatbuffer
 * of that type.
 */
template <typename T>
class Flatbuffer {
 public:
  Flatbuffer(flatbuffers::unique_ptr_t &&buffer, flatbuffers::uoffset_t size)
      : _buffer(std::move(buffer)),
        _size(size) {}

  static Flatbuffer fromBuilder(flatbuffers::FlatBufferBuilder *builder) {
    auto size = builder->GetSize();
    return Flatbuffer(builder->ReleaseBufferPointer(), size);
  }

  static std::shared_ptr<const Flatbuffer> sharedFromBuilder(
      flatbuffers::FlatBufferBuilder *builder) {
    return std::make_shared<const Flatbuffer>(fromBuilder(builder));
  }

  flatbuffers::BufferRef<T> ref() const {
    // Construct a non-owning BufferRef<T>
    return flatbuffers::BufferRef<T>(_buffer.get(), _size);
  }

 private:
  flatbuffers::unique_ptr_t _buffer;
  flatbuffers::uoffset_t _size;
};

class FlatbufferRefTransform {
 public:
  FlatbufferRefTransform() = delete;

  template <typename T>
  static std::shared_ptr<const T> wrap(flatbuffers::BufferRef<T> &&buffer) {
    if (!buffer.Verify()) {
      // TODO(peck): Handle this more gracefully
      return nullptr;
    } else {
      if (buffer.must_free) {
        buffer.must_free = false;
        uint8_t *buf = buffer.buf;
        return std::shared_ptr<const T>(
            buffer.GetRoot(),
            [buf](const T *) { free(buf); });
      } else {
        std::unique_ptr<uint8_t[]> copied_buffer(new uint8_t[buffer.len]);
        memcpy(copied_buffer.get(), buffer.buf, buffer.len);
        return std::shared_ptr<const T>(
            flatbuffers::GetRoot<T>(copied_buffer.get()),
            [buffer={std::move(copied_buffer)}](const T *) {});
      }
    }
  }

  template <typename T>
  static flatbuffers::BufferRef<T> unwrap(
      const std::shared_ptr<const Flatbuffer<T>> &ref) {
    return ref->ref();
  }
};

rxcpp::observable<
    std::shared_ptr<const Flatbuffer<ShkCache::ConfigGetResponse>>>
configGet(const std::shared_ptr<
    const ShkCache::ConfigGetRequest> &request) {
  flatbuffers::FlatBufferBuilder response_builder;
  auto store_config = ShkCache::CreateStoreConfig(
      response_builder,
      /*soft_store_entry_size_limit:*/1,
      /*hard_store_entry_size_limit:*/2);
  auto config_get_response = ShkCache::CreateConfigGetResponse(
      response_builder,
      store_config);

  response_builder.Finish(config_get_response);

  auto response = Flatbuffer<ShkCache::ConfigGetResponse>::sharedFromBuilder(
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

int main(int /*argc*/, const char * /*argv*/[]) {
  auto server = makeServer();
  std::thread server_thread([&] { server.run(); });

  auto channel = grpc::CreateChannel(
      "localhost:50051",
      grpc::InsecureChannelCredentials());

  RxGrpcClient client;

  auto config_client = client.makeClient<FlatbufferRefTransform>(
      ShkCache::Config::NewStub(channel));

  flatbuffers::FlatBufferBuilder fbb;
  auto config_get_request = ShkCache::CreateConfigGetRequest(fbb);
  fbb.Finish(config_get_request);
  auto request = Flatbuffer<ShkCache::ConfigGetRequest>::sharedFromBuilder(
      &fbb);

  config_client
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
          [&client]() {
            std::cout << "OnCompleted" << std::endl;
            client.shutdown();
          });

  client.run();

  server.shutdown();
  server_thread.join();

  return 0;
}

}  // namespace shk

int main(int argc, const char *argv[]) {
  return shk::main(argc, argv);
}
