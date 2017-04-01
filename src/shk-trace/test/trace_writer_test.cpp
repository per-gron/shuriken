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

    writer.fileEvent(EventType::Read, "path1", SymlinkBehavior::NO_FOLLOW);
    writer.fileEvent(EventType::Write, "path2", SymlinkBehavior::NO_FOLLOW);
  }

  char raw_buf[1024];
  int len = read(input_fd.get(), &raw_buf, 1024);

  auto trace = GetTrace(raw_buf);
  REQUIRE(trace->events()->size() == 2);
  CHECK(trace->events()->Get(0)->type() == EventType::Read);
  CHECK(std::string(trace->events()->Get(0)->path()->data()) == "path1");
  CHECK(trace->events()->Get(1)->type() == EventType::Write);
  CHECK(std::string(trace->events()->Get(1)->path()->data()) == "path2");
}

}  // namespace shk
