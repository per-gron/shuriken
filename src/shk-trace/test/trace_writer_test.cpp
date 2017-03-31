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
        new TracingServer::TraceRequest(std::move(output_fd), 0, "cwd")));

    writer.fileEvent(EventType::READ, "path1");
    writer.fileEvent(EventType::WRITE, "path2");
  }

  char raw_buf[1024];
  int len = read(input_fd.get(), &raw_buf, 1024);
  std::string buf(raw_buf, len);
  CHECK(buf == "read path1\nwrite path2\n");
}

}  // namespace shk
