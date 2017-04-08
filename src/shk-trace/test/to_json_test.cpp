#include <catch.hpp>

#include <errno.h>
#include <fcntl.h>
#include <fstream>
#include <unistd.h>
#include <streambuf>
#include <string>

#include <util/file_descriptor.h>
#include <util/shktrace.h>

#include "event_consolidator.h"
#include "to_json.h"

namespace shk {
namespace {

const char *kTestFilename = "tojson-tempfile";

std::string convert(const EventConsolidator &ec) {
  flatbuffers::FlatBufferBuilder builder(1024);
  builder.Finish(ec.generateTrace(builder));

  {
    FileDescriptor fd(open(kTestFilename, O_WRONLY | O_TRUNC | O_CLOEXEC | O_CREAT));
    CHECK(fd.get() != -1);
    CHECK(write(fd.get(), builder.GetBufferPointer(), builder.GetSize()) != -1);
  }

  std::string err;
  CHECK(convertOutputToJson(kTestFilename, &err));
  CHECK(err == "");

  std::ifstream input(kTestFilename);
  std::string str(
      (std::istreambuf_iterator<char>(input)),
       std::istreambuf_iterator<char>());
  input.close();

  return str;
}

}  // anonymous namespace

TEST_CASE("ToJson") {
  // In case a crashing test left a stale file behind.
  unlink(kTestFilename);
  EventConsolidator ec;

  SECTION("Empty") {
    CHECK(convert(ec) == "{}");
  }

  SECTION("Input") {
    ec.event(EventType::READ, "hej");
    CHECK(
      convert(ec) ==
      R"({"inputs":[{"path":"hej","directory_listing":false}]})");
  }

  SECTION("EscapeInput") {
    ec.event(EventType::READ, "h\"j");
    CHECK(
      convert(ec) ==
      R"({"inputs":[{"path":"h\"j","directory_listing":false}]})");
  }

  SECTION("InputDirListing") {
    ec.event(EventType::READ_DIRECTORY, "hej");
    CHECK(
      convert(ec) ==
      R"({"inputs":[{"path":"hej","directory_listing":true}]})");
  }

  SECTION("MultipleInputs") {
    ec.event(EventType::READ, "yo");
    ec.event(EventType::READ_DIRECTORY, "hej");
    auto json = convert(ec);
    if (
      json != R"({"inputs":[{"path":"hej","directory_listing":true},{"path":"yo","directory_listing":false}]})" &&
      json != R"({"inputs":[{"path":"yo","directory_listing":false},{"path":"hej","directory_listing":true}]})") {
      CHECK(json == "is not what we expected");
      CHECK(false);
    }
  }

  SECTION("Output") {
    ec.event(EventType::CREATE, "hej");
    CHECK(
      convert(ec) ==
      R"({"outputs":["hej"]})");
  }

  SECTION("EscapeOutput") {
    ec.event(EventType::CREATE, "h\"j");
    CHECK(
      convert(ec) ==
      R"({"outputs":["h\"j"]})");
  }

  SECTION("EscapeOutputAtEnd") {
    ec.event(EventType::CREATE, "h\"");
    CHECK(
      convert(ec) ==
      R"({"outputs":["h\""]})");
  }

  SECTION("EscapeOutputAtBeginning") {
    ec.event(EventType::CREATE, "\"j");
    CHECK(
      convert(ec) ==
      R"({"outputs":["\"j"]})");
  }

  SECTION("MultipleOutputs") {
    ec.event(EventType::CREATE, "hej");
    ec.event(EventType::CREATE, "yo");
    auto json = convert(ec);
    if (
      json != R"({"outputs":["yo","hej"]})" &&
      json != R"({"outputs":["hej","yo"]})") {
      CHECK(json == "is not what we expected");
      CHECK(false);
    }
  }

  SECTION("Error") {
    ec.event(EventType::FATAL_ERROR, "hej");
    CHECK(
      convert(ec) ==
      R"({"errors":["hej"]})");
  }

  SECTION("EscapeError") {
    ec.event(EventType::FATAL_ERROR, "h\"j");
    CHECK(
      convert(ec) ==
      R"({"errors":["h\"j"]})");
  }

  SECTION("MultipleErrors") {
    ec.event(EventType::FATAL_ERROR, "hej");
    ec.event(EventType::FATAL_ERROR, "yo");
    CHECK(
        convert(ec) ==
        R"({"errors":["hej","yo"]})");
  }

  SECTION("AllCombined") {
    ec.event(EventType::READ, "1");
    ec.event(EventType::CREATE, "2");
    ec.event(EventType::FATAL_ERROR, "3");
    CHECK(
      convert(ec) ==
      R"({"inputs":[{"path":"1","directory_listing":false}],"outputs":["2"],"errors":["3"]})");
  }

  unlink(kTestFilename);
}

}  // namespace shk
