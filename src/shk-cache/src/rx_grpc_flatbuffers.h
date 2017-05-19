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

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/grpc.h>

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

}  // namespace shk
