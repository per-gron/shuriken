#include <catch.hpp>

#include <util/shktrace.h>

namespace shk {

std::vector<uint8_t> constructAndSerializeObject() {
  flatbuffers::FlatBufferBuilder builder(1024);

  std::vector<flatbuffers::Offset<Input>> input_offsets;
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
