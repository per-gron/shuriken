#include "tracing_server.h"

#include <bsm/libbsm.h>

#include "fileport.h"

namespace shk {
namespace {

struct MachSendMsg {
  mach_msg_header_t header;
  mach_msg_body_t body;
  mach_msg_port_descriptor_t trace_fd_port;
  mach_msg_port_descriptor_t trace_ack_port;
  char cwd[2048];
};

struct MachRecvMsg : public MachSendMsg {
  mach_msg_audit_trailer_t trailer;
};

struct MachAckMsg {
  mach_msg_header_t header;
  char data[4];
};

struct MachRecvAckMsg : public MachAckMsg {
  void *dummy;
};

void sendAck(const MachSendRight &ack_port) {
  MachAckMsg msg;
  bzero(&msg, sizeof(msg));
  msg.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
  msg.header.msgh_size = sizeof(msg);
  msg.header.msgh_remote_port = ack_port.get();
  msg.header.msgh_local_port = MACH_PORT_NULL;
  msg.data[0] = 'A';
  msg.data[1] = 'C';
  msg.data[2] = 'K';
  msg.data[3] = '\0';

  auto kr = mach_msg(
      &msg.header,
      MACH_SEND_MSG,
      /* send size: */msg.header.msgh_size,
      /* receive size: */0,
      /* receive name: */MACH_PORT_NULL,
      /* timeout: */MACH_MSG_TIMEOUT_NONE,
      /* notification port: */MACH_PORT_NULL);
  if (kr == MACH_SEND_INVALID_DEST) {
    // The ack port is not valid. This likely means that the MachTraceHandle
    // for this tracing request has gone away before the tracing was finished.
    // This is okay.
  } else if (kr != KERN_SUCCESS) {
    // Failed to send ack to client.
    fprintf(stderr, "sendAck(): mach_msg(): %s\n", mach_error_string(kr));
  }
}

class GCDTraceRequest : public TracingServer::TraceRequest {
 public:
  GCDTraceRequest(
      FileDescriptor &&trace_fd,
      pid_t pid_to_trace,
      std::string &&cwd,
      MachSendRight &&ack_port)
      : TraceRequest(std::move(trace_fd), pid_to_trace, std::move(cwd)),
        _ack_port(std::move(ack_port)) {}

  virtual ~GCDTraceRequest() {
    sendAck(_ack_port);
  }

 private:
  MachSendRight _ack_port;
};

class GCDTracingServer : public TracingServer {
 public:
  GCDTracingServer(
      dispatch_queue_t queue,
      MachReceiveRight &&mach_port,
      const TracingServer::Callback &cb)
      : _mach_port(std::move(mach_port)),
        _cb(cb),
        _port_source(dispatch_source_create(
            DISPATCH_SOURCE_TYPE_MACH_RECV,
            _mach_port.get(),
            0,
            queue)) {}

  GCDTracingServer(const GCDTracingServer &) = delete;
  GCDTracingServer& operator=(const GCDTracingServer &) = delete;

  void start() {
    dispatch_source_set_event_handler(_port_source.get(), ^{
      handleMessage();
    });
    dispatch_resume(_port_source.get());
  }

 private:
  void handleMessage() {
    MachRecvMsg msg;
    if (!receiveMessage(msg)) {
      // Failure.
      return;
    }

    // Assume ownership of the remote port. When this function returns,
    // it will be deallocated, which causes the caller to continue,
    MachSendRight tracing_started_ack_port(msg.header.msgh_remote_port);
    MachSendRight tracing_finished_ack_port(msg.trace_ack_port.name);

    // Use the kernel audit information to find out which process it was that
    // requested tracing. The audit token contains an unspoofable pid.
    pid_t client_pid = audit_token_to_pid(msg.trailer.msgh_audit);

    // Get the file descriptor to write the tracing results to
    MachSendRight trace_fd_port(msg.trace_fd_port.name);
    FileDescriptor trace_fd(fileport_makefd(trace_fd_port.get()));

    // Invoke tracing callback
    _cb(std::unique_ptr<TraceRequest>(new GCDTraceRequest(
        std::move(trace_fd),
        client_pid,
        msg.cwd,
        std::move(tracing_finished_ack_port))));
  }

  bool receiveMessage(MachRecvMsg &msg) {
    bzero(&msg, sizeof(msg));
    msg.header.msgh_size = sizeof(msg);
    msg.header.msgh_local_port = _mach_port.get();

    mach_msg_option_t options = MACH_RCV_MSG |
        MACH_RCV_TRAILER_TYPE(MACH_RCV_TRAILER_AUDIT) |
        MACH_RCV_TRAILER_ELEMENTS(MACH_RCV_TRAILER_AUDIT);

    auto kr = mach_msg(
        &msg.header,
        options,
        0,
        sizeof(msg),
        _mach_port.get(),
        MACH_MSG_TIMEOUT_NONE,
        MACH_PORT_NULL);

    if (kr != KERN_SUCCESS) {
      // Failed to read message from client.
      fprintf(
          stderr, "receiveMessage(): mach_msg(): %s\n", mach_error_string(kr));
      return false;
    }
    return true;
  }

  const MachReceiveRight _mach_port;
  const TracingServer::Callback _cb;
  const DispatchSource _port_source;
};

class MachTraceHandle : public TraceHandle {
 public:
  MachTraceHandle(MachReceiveRight &&ack_port)
      : _ack_port(std::move(ack_port)) {}

  virtual WaitResult wait(mach_msg_timeout_t timeout) override {
    MachRecvAckMsg msg;
    bzero(&msg, sizeof(msg));
    msg.header.msgh_size = sizeof(msg);
    msg.header.msgh_local_port = _ack_port.get();

    auto kr = mach_msg(
        &msg.header,
        MACH_RCV_MSG | (timeout == MACH_MSG_TIMEOUT_NONE ? 0 : MACH_RCV_TIMEOUT),
        /* send size: */0,
        /* receive size: */msg.header.msgh_size,
        /* receive name: */_ack_port.get(),
        /* timeout: */timeout,
        /* notification port: */MACH_PORT_NULL);

    if (kr == MACH_RCV_TIMED_OUT) {
      return WaitResult::TIMED_OUT;
    } else if (kr != KERN_SUCCESS) {
      // Failed to send ack to client.
      fprintf(stderr, "waitForAck(): mach_msg(): %s\n", mach_error_string(kr));
      return WaitResult::FAILURE;
    }

    if (strcmp("ACK", msg.data) != 0) {
      return WaitResult::FAILURE;
    }

    return WaitResult::SUCCESS;
  }

 private:
  MachReceiveRight _ack_port;
};

}  // anonymous namespace

std::unique_ptr<TracingServer> makeTracingServer(
    dispatch_queue_t queue,
    MachReceiveRight &&port,
    const TracingServer::Callback &cb) {
  auto server = new GCDTracingServer(
      queue, std::move(port), cb);
  server->start();
  return std::unique_ptr<TracingServer>(server);
}

std::pair<std::unique_ptr<TraceHandle>, MachOpenPortResult> requestTracing(
      const MachSendRight &server_port,
      FileDescriptor &&trace_fd,
      const std::string &cwd) {
  // Make mach port to receive the tracing start acknowledgement on
  mach_port_t raw_receive_port;
  if (mach_port_allocate(
          mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &raw_receive_port)) {
    return std::make_pair(nullptr, MachOpenPortResult::FAILURE);
  }
  MachReceiveRight receive_port(raw_receive_port);

  // Make mach port to receive the tracing finish acknowledgement on
  auto ack_ports = makePortPair();

  // Make mach port to transfer the trace_fd
  mach_port_t raw_fd_port;
  if (fileport_makeport(trace_fd.get(), &raw_fd_port)) {
    return std::make_pair(nullptr, MachOpenPortResult::FAILURE);
  }
  MachSendRight fd_port(raw_fd_port);

  MachSendMsg send_msg;
  bzero(&send_msg, sizeof(send_msg));
  send_msg.header.msgh_bits =
      MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, MACH_MSG_TYPE_MAKE_SEND_ONCE) |
      MACH_MSGH_BITS_COMPLEX;
  send_msg.header.msgh_size = sizeof(send_msg);
  send_msg.header.msgh_remote_port = server_port.get();
  send_msg.header.msgh_local_port = receive_port.get();
  send_msg.header.msgh_reserved = 0;
  send_msg.header.msgh_id = 0;
  send_msg.body.msgh_descriptor_count = 2;

  send_msg.trace_fd_port.name = fd_port.release();
  send_msg.trace_fd_port.disposition = MACH_MSG_TYPE_PORT_SEND;
  send_msg.trace_fd_port.type = MACH_MSG_PORT_DESCRIPTOR;

  send_msg.trace_ack_port.name = ack_ports.second.release();
  send_msg.trace_ack_port.disposition = MACH_MSG_TYPE_PORT_SEND;
  send_msg.trace_ack_port.type = MACH_MSG_PORT_DESCRIPTOR;

  if (cwd.size() >= sizeof(send_msg.cwd)) {
    // Cwd too large
    return std::make_pair(nullptr, MachOpenPortResult::FAILURE);
  }
  strncpy(send_msg.cwd, cwd.c_str(), sizeof(send_msg.cwd));

  // Send a mach message to the other port and wait for a reply from the other
  // process. The reply is sent when tracing is actually enabled.
  auto kr = mach_msg(
      &send_msg.header,
      MACH_SEND_MSG | MACH_RCV_MSG,
      /* send size: */ send_msg.header.msgh_size,
      /* receive size: */send_msg.header.msgh_size,
      /* receive name: */receive_port.get(),
      /* timeout: */MACH_MSG_TIMEOUT_NONE,
      /* notification port: */MACH_PORT_NULL);

  if (kr != KERN_SUCCESS) {
    // Failed to send request to server.
    fprintf(stderr, "requestTracing(): mach_msg(): %s\n", mach_error_string(kr));
    return std::make_pair(nullptr, MachOpenPortResult::FAILURE);
  }

  return std::make_pair(
      std::unique_ptr<TraceHandle>(
          new MachTraceHandle(std::move(ack_ports.first))),
      MachOpenPortResult::SUCCESS);
}

}  // namespace shk
