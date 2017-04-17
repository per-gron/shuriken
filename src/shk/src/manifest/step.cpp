#include "manifest/step.h"

#include <blake2.h>

namespace shk {

Step::Step(std::shared_ptr<flatbuffers::FlatBufferBuilder> &&data)
    : _data(std::move(data)),
      _step(flatbuffers::GetRoot<ShkManifest::Step>(
          _data->GetBufferPointer())) {}

}  // namespace shk
