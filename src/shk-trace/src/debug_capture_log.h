#pragma once

#include <functional>
#include <string>

#include <util/file_descriptor.h>

#include "kdebug.h"
#include "tracing_server.h"

namespace shk {

/**
 * Helper class for reading and writing a debug capture log. A debug capture log
 * has all the information that a shk-trace server process receives from Kdebug,
 * along with all the tracing requests that it received at the time. With such
 * a log it is possible to replay whatever a shk-trace server process has done,
 * without having to use Kdebug and trigger the same system behavior again. This
 * can be useful to debug bugs that only occur occasionally and to make it
 * possible to attach shk-trace to a normal debugger.
 *
 * Because this is intended for debugging, it does not have careful error
 * handling, it just aborts if things go wrong when writing.
 */
class DebugCaptureLog {
 public:
  DebugCaptureLog(FileDescriptor &&fd);

  void writeTraceRequest(const TracingServer::TraceRequest &trace_request);
  void writeKdBufs(const kd_buf *begin, const kd_buf *end);

  static bool parse(
      const FileDescriptor &fd,
      const std::function<
          void (std::unique_ptr<TracingServer::TraceRequest> &&)> &
              trace_request_callback,
      const std::function<void (
          const kd_buf *begin, const kd_buf *end)> &kd_bufs_callback,
      std::string *err);
 private:
  void writeToFile(void *buf, size_t size);

  FileDescriptor _fd;
};

}  // namespace shk
