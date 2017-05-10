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

#include <catch.hpp>

#include <util/shktrace.h>

namespace shk {

std::vector<uint8_t> constructAndSerializeObject() {
  flatbuffers::FlatBufferBuilder builder(1024);

  std::vector<flatbuffers::Offset<flatbuffers::String>> input_offsets;
  auto input_vector = builder.CreateVector(input_offsets.data(), 0);

  flatbuffers::Offset<flatbuffers::String> output_offsets =
      builder.CreateString("path");
  auto output_vector = builder.CreateVector(&output_offsets, 1);

  std::vector<flatbuffers::Offset<flatbuffers::String>> error_offsets;
  auto error_vector = builder.CreateVector(error_offsets.data(), 0);

  builder.Finish(CreateTrace(
      builder, input_vector, output_vector, error_vector));

  return std::vector<uint8_t>(
      builder.GetBufferPointer(),
      builder.GetBufferPointer() + builder.GetSize());
}

TEST_CASE("ShkTrace") {
  auto buffer = constructAndSerializeObject();

  auto trace = GetTrace(buffer.data());

  CHECK(trace->inputs()->size() == 0);
  CHECK(trace->errors()->size() == 0);
  REQUIRE(trace->outputs()->size() == 1);
  CHECK(std::string(trace->outputs()->Get(0)->c_str()) == "path");
}

}
