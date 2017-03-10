#pragma once

#include <functional>

#include <dispatch/dispatch.h>

#include "dispatch.h"
#include "file_descriptor.h"
#include "mach_port.h"
#include "named_mach_port.h"

namespace shk {

/**
 * The TracingServer interface doesn't really do much other than than offer
 * controls for lifetime (a destructor). For more info, see makeTracingServer.
 */
class TracingServer {
 public:
  virtual ~TracingServer() = default;

  /**
   * A TraceRequest object represents a request from a process that wants to be
   * traced. It contains the pid to trace (which validity should be already
   * verified by now) and a file descriptor to write the tracing results to.
   *
   * When the tracing is done (the first child process of that process has died)
   * the object should be destroyed, which will signal to the client that the
   * tracing has finished and it's safe to read the tracing file.
   */
  class TraceRequest {
   public:
    TraceRequest(FileDescriptor &&trace_fd, pid_t pid_to_trace)
        : trace_fd(std::move(trace_fd)), pid_to_trace(pid_to_trace) {}
    virtual ~TraceRequest() = default;

    const FileDescriptor trace_fd;
    const pid_t pid_to_trace;
  };

  /**
   * A callback that is invoked by the tracing server when a request to trace
   * has been received. When the callback returns, the server sends an
   * acknowledgement message to the client that requested tracing indicating
   * that it can begin, so the callback must make sure that tracing is enabled
   * before returning.
   */
  using Callback = std::function<void (std::unique_ptr<TraceRequest> &&)>;
};

/**
 * Construct a TracingServer that listens for shk-trace tracing requests on a
 * given Mach port.
 */
std::unique_ptr<TracingServer> makeTracingServer(
    dispatch_queue_t queue,
    MachReceiveRight &&port,
    const TracingServer::Callback &cb);

/**
 * A TraceHandle is owned by a process that has asked to be traced. Destroying
 * it has no effect. The reason it exists is that it allows the traced process
 * to wait for the tracing to finish, using the wait() method.
 */
class TraceHandle {
 public:
  virtual ~TraceHandle() = default;

  enum class WaitResult {
    SUCCESS,
    TIMED_OUT,
    FAILURE
  };

  /**
   * Wait for tracing to finish. This is a blocking operation.
   */
  virtual WaitResult wait(mach_msg_timeout_t timeout) = 0;
};

/**
 * Request tracing of the first child process of this process. Tracing results
 * are written to trace_fd.
 *
 * This function blocks and returns only when the server has acknowledged that
 * the tracing has begun (or on failure).
 */
std::pair<std::unique_ptr<TraceHandle>, MachOpenPortResult> requestTracing(
    const MachSendRight &server_port, FileDescriptor &&trace_fd);

}  // namespace shk
