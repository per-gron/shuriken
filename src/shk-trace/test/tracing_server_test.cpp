#include <catch.hpp>

#include <vector>

#include "dispatch.h"
#include "named_mach_port.h"
#include "tracing_server.h"

namespace shk {

static int test_counter = 0;

TEST_CASE("TracingServer") {
  DispatchQueue queue(dispatch_queue_create(
      "TracingServerTest",
      DISPATCH_QUEUE_SERIAL));

  auto port_pair = makePortPair();
  REQUIRE(port_pair.first.get() != MACH_PORT_NULL);
  REQUIRE(port_pair.second.get() != MACH_PORT_NULL);

  DispatchSemaphore sema(dispatch_semaphore_create(0));

  std::vector<std::unique_ptr<TracingServer::TraceRequest>> requests;

  auto server = makeTracingServer(queue, std::move(port_pair.first), [&](
      std::unique_ptr<TracingServer::TraceRequest> &&request) {
    requests.push_back(std::move(request));
    dispatch_semaphore_signal(sema.get());
  });

  int fd[2];
  REQUIRE(pipe(fd) == 0);
  FileDescriptor input_fd(fd[0]);
  FileDescriptor output_fd(fd[1]);

  auto request = requestTracing(
      std::move(port_pair.second), std::move(output_fd));
  REQUIRE(request.second == MachOpenPortResult::SUCCESS);

  if (dispatch_semaphore_wait(
          sema.get(),
          dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC)) != 0) {
    REQUIRE(!"Waiting for request to arrive timed out");
  }
  REQUIRE(requests.size() == 1);

  SECTION("TransferFileDescriptor") {
    REQUIRE(write(requests.front()->trace_fd.get(), "!", 1) == 1);
    char buf;
    REQUIRE(read(input_fd.get(), &buf, 1) == 1);
    CHECK(buf == '!');
  }

  SECTION("CheckPid") {
    // This is a rather lame test. We could as well be getting the pid of the
    // tracing server...
    CHECK(requests.front()->pid_to_trace == getpid());
  }

  SECTION("WaitForTracing") {
    SECTION("DontWait") {
      requests.clear();
    }

    SECTION("NeverAcked") {
      CHECK(
          request.first->wait(500/* milliseconds*/) ==
          TraceHandle::WaitResult::TIMED_OUT);
    }

    SECTION("Acked") {
      requests.clear();
      CHECK(
          request.first->wait(MACH_MSG_TIMEOUT_NONE) ==
          TraceHandle::WaitResult::SUCCESS);
    }
  }
}

}  // namespace shk
