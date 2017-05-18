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

  const T &operator*() const {
    return *flatbuffers::GetRoot<T>(_buffer.get());
  }

  const T *operator->() const {
    return &**this;
  }

 private:
  flatbuffers::unique_ptr_t _buffer;
  flatbuffers::uoffset_t _size;
};

template <typename T>
using FlatbufferPtr = std::shared_ptr<const Flatbuffer<T>>;

class FlatbufferRefTransform {
 public:
  FlatbufferRefTransform() = delete;

  template <typename T>
  static std::pair<FlatbufferPtr<T>, grpc::Status> wrap(
      flatbuffers::BufferRef<T> &&buffer) {
    if (!buffer.Verify()) {
      return std::make_pair(
          nullptr,
          grpc::Status(grpc::DATA_LOSS, "Got invalid Flatbuffer data"));
    } else {
      if (buffer.must_free) {
        // buffer owns its memory. Steal its memory
        buffer.must_free = false;
        uint8_t *buf = buffer.buf;
        return std::make_pair(
            std::make_shared<const Flatbuffer<T>>(
                flatbuffers::unique_ptr_t(
                    buf,
                    [buf](const uint8_t *) { free(buf); }),
                buffer.len),
            grpc::Status::OK);
      } else {
        // buffer does not own its memory. We need to copy
        auto copied_buffer = flatbuffers::unique_ptr_t(
            new uint8_t[buffer.len],
            [](const uint8_t *buf) { delete[] buf; });
        memcpy(copied_buffer.get(), buffer.buf, buffer.len);

        return std::make_pair(
            std::make_shared<const Flatbuffer<T>>(
                std::move(copied_buffer), buffer.len),
            grpc::Status::OK);
      }
    }
  }

  template <typename T>
  static flatbuffers::BufferRef<T> unwrap(
      const FlatbufferPtr<T> &ref) {
    return ref->ref();
  }
};

auto configGet(const FlatbufferPtr<ShkCache::ConfigGetRequest> &request) {
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
          &configGet)
      .registerMethod<FlatbufferRefTransform>(
          &ShkCache::Config::AsyncService::RequestServerStream,
          &configGet);

  return builder.buildAndStart();
}

auto makeConfigGetRequest() {
  flatbuffers::FlatBufferBuilder fbb;
  auto config_get_request = ShkCache::CreateConfigGetRequest(fbb);
  fbb.Finish(config_get_request);
  return Flatbuffer<ShkCache::ConfigGetRequest>::sharedFromBuilder(
      &fbb);
}

int main(int /*argc*/, const char * /*argv*/[]) {
  // TODO(peck): Add support for making more than one request
  // TODO(peck): Add support for streaming from server (in client)
  // TODO(peck): Add support for streaming from client (in server)
  // TODO(peck): Add support for streaming from client (in client)
  // TODO(peck): Add support for bidi streaming (in server)
  // TODO(peck): Add support for bidi streaming (in client)
  // TODO(peck): Add support for cancellation (cancel is called unsubscribe)
  // TODO(peck): Add support for backpressure (streaming output requires only
  //     one outstanding request at a time. Not possible atm.)
  // TODO(peck): Test

  auto server = makeServer();
  std::thread server_thread([&] { server.run(); });

  auto channel = grpc::CreateChannel(
      "localhost:50051",
      grpc::InsecureChannelCredentials());

  RxGrpcClient client;  // TODO(peck): Rename to ClientFactory

  auto config_client = client.makeClient<FlatbufferRefTransform>(
      ShkCache::Config::NewStub(channel));

  config_client
      .invoke(
          &ShkCache::Config::Stub::AsyncServerStream, makeConfigGetRequest())
      .subscribe(
          [](const FlatbufferPtr<ShkCache::ConfigGetResponse> &response) {
            if (auto config = (*response)->config()) {
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
          [&client](std::exception_ptr error) {
            // TODO(peck): Do something useful with this
            std::cout << "OnError" << std::endl;
            client.shutdown();
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
