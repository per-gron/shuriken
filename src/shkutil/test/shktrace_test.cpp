#include <catch.hpp>

#include <util/shktrace.h>

namespace shk {

std::vector<uint8_t> constructAndSerializeObject() {
  flatbuffers::FlatBufferBuilder builder(1024);

  auto path_name = builder.CreateString("path");
  auto event = CreateEvent(builder, EventType::Write, path_name);
  auto events = builder.CreateVector(&event, 1);
  auto trace = CreateTrace(builder, events);
  builder.Finish(trace);

  return std::vector<uint8_t>(
      builder.GetBufferPointer(),
      builder.GetBufferPointer() + builder.GetSize());
}

TEST_CASE("ShkTrace") {
  auto buffer = constructAndSerializeObject();

  auto trace = GetTrace(buffer.data());

  REQUIRE(trace->events()->size() == 1);
  CHECK(trace->events()->Get(0)->type() == EventType::Write);
  CHECK(std::string(trace->events()->Get(0)->path()->data()) == "path");
}

}
