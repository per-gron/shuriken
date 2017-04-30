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

#include <vector>

#include "debug_capture_log.h"

namespace shk {
namespace {

const char *kTestFilename = "capturelog-tempfile";

void parseExpectFail() {
  auto fd = FileDescriptor(open(kTestFilename, O_RDONLY));
  CHECK(fd.get() != -1);

  std::string err;
  CHECK(!DebugCaptureLog::parse(
      fd,
      [](std::unique_ptr<TracingServer::TraceRequest> &&trace_request) {},
      [](const kd_buf *begin, const kd_buf *end) {},
      &err));
  CHECK(err != "");
}

void parse(
    const std::function<
        void (std::unique_ptr<TracingServer::TraceRequest> &&)> &
            trace_request_callback,
    const std::function<void (
        const kd_buf *begin, const kd_buf *end)> &kd_bufs_callback) {
  auto fd = FileDescriptor(open(kTestFilename, O_RDONLY));
  CHECK(fd.get() != -1);

  std::string err;
  CHECK(DebugCaptureLog::parse(
      fd, trace_request_callback, kd_bufs_callback, &err));
  CHECK(err == "");
}

std::vector<kd_buf> parseOneKdbufsEntry() {
  std::vector<kd_buf> ans;
  bool invoked = false;

  parse(
      [&](std::unique_ptr<TracingServer::TraceRequest> &&trace_request) {
        CHECK(false);
      },
      [&](const kd_buf *begin, const kd_buf *end) {
        CHECK(!invoked);
        invoked = true;
        ans = std::vector<kd_buf>(begin, end);
      });

  CHECK(invoked);
  return ans;
}

std::unique_ptr<TracingServer::TraceRequest> parseOneTraceRequestEntry() {
  std::unique_ptr<TracingServer::TraceRequest> ans;
  bool invoked = false;

  parse(
      [&](std::unique_ptr<TracingServer::TraceRequest> &&trace_request) {
        CHECK(!invoked);
        invoked = true;
        ans = std::move(trace_request);
      },
      [&](const kd_buf *begin, const kd_buf *end) {
        CHECK(false);
      });

  CHECK(invoked);
  return ans;
}

}  // anonymous namespace

TEST_CASE("DebugCaptureLog") {
  // In case a crashing test left a stale file behind.
  unlink(kTestFilename);

  auto log_fd = open(
      kTestFilename, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
  CHECK(log_fd != -1);
  auto log = std::unique_ptr<DebugCaptureLog>(
      new DebugCaptureLog(FileDescriptor(log_fd)));

  SECTION("empty") {
    log.reset();  // close the log fd

    parse(
        [](std::unique_ptr<TracingServer::TraceRequest> &&trace_request) {
          CHECK(false);
        },
        [](const kd_buf *begin, const kd_buf *end) {
          CHECK(false);
        });
  }

  SECTION("wrong header length") {
    char dummy = 1;
    write(log_fd, &dummy, sizeof(dummy));

    log.reset();  // close the log fd

    parseExpectFail();
  }

  SECTION("wrong entry length") {
    size_t size = 14;
    write(log_fd, &size, sizeof(size));

    log.reset();  // close the log fd

    parseExpectFail();
  }

  SECTION("invalid flatbuffer") {
    size_t size = sizeof(size);
    write(log_fd, &size, sizeof(size));
    write(log_fd, &size, sizeof(size));

    log.reset();  // close the log fd

    parseExpectFail();
  }

  SECTION("TraceRequest") {
    log->writeTraceRequest(TracingServer::TraceRequest(
        FileDescriptor(-1), 123, 345, "cwd"));
    log.reset();  // close the log fd

    auto trace_request = parseOneTraceRequestEntry();
    CHECK(trace_request->pid_to_trace == 123);
    CHECK(trace_request->root_thread_id == 345);
    CHECK(trace_request->cwd == "cwd");
  }

  SECTION("kd_bufs") {
    SECTION("empty") {
      log->writeKdBufs(nullptr, nullptr);
      log.reset();  // close the log fd

      auto ans = parseOneKdbufsEntry();
      CHECK(ans.empty());
    }

    SECTION("single buffer") {
      kd_buf original_buf{};
      original_buf.timestamp = 123;
      log->writeKdBufs(&original_buf, &original_buf + 1);
      log.reset();  // close the log fd

      auto ans = parseOneKdbufsEntry();
      REQUIRE(ans.size() == 1);
      CHECK(ans.front().timestamp == 123);
    }

    SECTION("two buffers") {
      kd_buf original_bufs[] = { {}, {} };
      original_bufs[0].timestamp = 123;
      original_bufs[1].timestamp = 321;
      log->writeKdBufs(original_bufs, original_bufs + 2);
      log.reset();  // close the log fd

      auto ans = parseOneKdbufsEntry();
      REQUIRE(ans.size() == 2);
      CHECK(ans[0].timestamp == 123);
      CHECK(ans[1].timestamp == 321);
    }

    SECTION("wrong length") {
      kd_buf original_buf{};
      log->writeKdBufs(&original_buf, &original_buf + 1);

      char dummy = 1;
      write(log_fd, &dummy, sizeof(dummy));

      log.reset();  // close the log fd

      parseExpectFail();
    }
  }

  unlink(kTestFilename);
}

}  // namespace shk
