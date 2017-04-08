#include <catch.hpp>

#include "trace_writer.h"

namespace shk {

TEST_CASE("TraceWriter") {
  int fd[2];
  REQUIRE(pipe(fd) == 0);
  FileDescriptor input_fd(fd[0]);
  FileDescriptor output_fd(fd[1]);

  {
    TraceWriter writer(std::unique_ptr<TracingServer::TraceRequest>(
        new TracingServer::TraceRequest(std::move(output_fd), 0, 0, "cwd")));

    writer.fileEvent(EventType::READ, "path1");
    writer.fileEvent(EventType::CREATE, "path2");
  }

  char raw_buf[1024];
  int len = read(input_fd.get(), &raw_buf, 1024);

  auto trace = GetTrace(raw_buf);
  REQUIRE(trace->inputs()->size() == 1);
  CHECK(std::string(trace->inputs()->Get(0)->data()) == "path1");

  REQUIRE(trace->outputs()->size() == 1);
  CHECK(std::string(trace->outputs()->Get(0)->data()) == "path2");

  CHECK(trace->errors()->size() == 0);
}

}  // namespace shk
