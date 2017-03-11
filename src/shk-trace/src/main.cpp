#include <errno.h>
#include <libc.h>
#include <spawn.h>
#include <string.h>
#include <thread>

#include "event_consolidator.h"
#include "kdebug_controller.h"
#include "named_mach_port.h"
#include "process_tracer.h"
#include "tracer.h"
#include "tracing_server.h"

extern "C" int reexec_to_match_kernel();
extern char **environ;

namespace shk {
namespace {

int getNumCpus() {
  int num_cpus;
  size_t len = sizeof(num_cpus);
  static int name[] = { CTL_HW, HW_NCPU, 0 };
  if (sysctl(name, 2, &num_cpus, &len, nullptr, 0) < 0) {
    throw std::runtime_error("Failed to get number of CPUs");
  }
  return num_cpus;
}

static const std::string PORT_NAME = "com.pereckerdal.shktrace";

class ProcessTracerDelegate : public ProcessTracer::Delegate {
 public:
  ProcessTracerDelegate(std::unique_ptr<TracingServer::TraceRequest> &&request)
      : _request(std::move(request)) {}

  virtual ~ProcessTracerDelegate() {
    auto events = _consolidator.getConsolidatedEventsAndReset();
    for (const auto &event : events) {
      // TODO(peck): Write something that actually makes sense
      write(event.second + "\n");
    }

    // TODO(peck): Do something about quitting the tracing server as well.
  }

  virtual void fileEvent(Tracer::EventType type, std::string &&path) override {
    _consolidator.event(type, std::move(path));
  }

 private:
  void write(const std::string &str) {
    auto written = ::write(
        _request->trace_fd.get(),
        str.c_str(),
        str.size());
    if (written != str.size()) {
      fprintf(stderr, "Failed to write to tracing file\n");
      abort();
    }
  }

  // This object is destroyed when tracing has finished. That, in turn, will
  // destroy the TraceRequest, which signals to the traced process that tracing
  // has finished.
  const std::unique_ptr<TracingServer::TraceRequest> _request;
  EventConsolidator _consolidator;
};

std::unique_ptr<TracingServer> runTracingServer(
    dispatch_queue_t queue, MachReceiveRight &&port) {
  auto kdebug_ctrl = makeKdebugController();

  ProcessTracer process_tracer;

  Tracer tracer(
      getNumCpus(),
      *kdebug_ctrl,
      process_tracer);
  tracer.start(queue);

  return makeTracingServer(
      queue,
      std::move(port),
      [&](std::unique_ptr<TracingServer::TraceRequest> &&request) {
        pid_t pid = request->pid_to_trace;
        process_tracer.traceProcess(
            pid,
            std::unique_ptr<ProcessTracer::Delegate>(
                new ProcessTracerDelegate(std::move(request))));
      });
}

std::pair<MachSendRight, bool> connectToServer() {
  auto client_port = openNamedPort(PORT_NAME);
  if (client_port.second == MachOpenPortResult::SUCCESS) {
    return std::make_pair(std::move(client_port.first), true);
  } else if (client_port.second != MachOpenPortResult::NOT_FOUND) {
    fprintf(stderr, "Failed to open Mach port against server\n");
    return std::make_pair(MachSendRight(), false);
  }

  auto server_port = registerNamedPort(PORT_NAME);
  // TODO(peck): handle failure
  // runTracingServer(std::move(server_port.first));

  printf("Need to open\n");
  return std::make_pair(MachSendRight(), false);
}

void dropPrivileges() {
  uid_t newuid = getuid();
  uid_t olduid = geteuid();

  if (newuid != olduid) {
    seteuid(newuid);
    if (setuid(newuid) == -1) {
      abort();
    }
  }
}

int executeCommand(const std::string &cmd) {
  pid_t pid;
  const char *argv[] = { "sh", "-c", cmd.c_str(), NULL };
  int err = posix_spawn(
      &pid,
      "/bin/sh",
      nullptr,
      nullptr,
      const_cast<char **>(argv),
      environ);
  if (err) {
    fprintf(stderr, "Failed to spawn child process: %s\n", strerror(errno));
    return 1;
  }

  int status;
  if (waitpid(pid, &status, 0) == -1) {
    fprintf(stderr, "Failed to wait for child process: %s\n", strerror(errno));
    return 1;
  }

  return WEXITSTATUS(status);
}

std::pair<FileDescriptor, bool> openTraceFile(const std::string &path) {
  int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
  return std::make_pair(FileDescriptor(fd), fd != -1);
}

int main(int argc, char *argv[]) {
  if (0 != reexec_to_match_kernel()) {
    fprintf(stderr, "Could not re-execute: %s\n", strerror(errno));
    return 1;
  }

  if (getgid() != getegid()) {
    fprintf(stderr, "This tool must not be run with setgid bit set\n");
    return 1;
  }

  if (geteuid() != 0) {
    fprintf(stderr, "This tool must be run as root\n");
    return 1;
  }

  auto port_pair = makePortPair();
  DispatchQueue queue(dispatch_queue_create(
      "shk-trace-server",
      DISPATCH_QUEUE_SERIAL));

  auto kdebug_ctrl = makeKdebugController();

  ProcessTracer process_tracer;

  Tracer tracer(
      getNumCpus(),
      *kdebug_ctrl,
      process_tracer);
  tracer.start(queue.get());

  auto tracing_server = makeTracingServer(
      queue.get(),
      std::move(port_pair.first),
      [&](std::unique_ptr<TracingServer::TraceRequest> &&request) {
        pid_t pid = request->pid_to_trace;
        process_tracer.traceProcess(
            pid,
            std::unique_ptr<ProcessTracer::Delegate>(
                new ProcessTracerDelegate(std::move(request))));
      });

  //auto mach_port = connectToServer();
  /*if (!mach_port.second) {
    TODO(peck): Do error checking here
    return 1;
  }*/

  dropPrivileges();

  auto trace_fd = openTraceFile("trace.txt");
  if (!trace_fd.second) {
    fprintf(stderr, "Failed to open tracing file: %s\n", strerror(errno));
    return 1;
  }

  auto trace_request = requestTracing(
      std::move(port_pair.second),
      std::move(trace_fd.first));
  if (trace_request.second != MachOpenPortResult::SUCCESS) {
    fprintf(stderr, "Failed to initiate tracing\n");
    return 1;
  }

  int status_code;
  std::thread([&] {
    // Due to a limitation in the tracing information that kdebug provides,
    // the traced program must create a thread, that has its own pid, which
    // will be traced. This thread can then spawn another process.
    //
    // This is because a tracing request contains the pid of the process to
    // be traced. This starts tracing 
    //
    // TODO(peck): Restructure / document this so that it becomes sane.
    printf("EXECUTING\n");

    status_code = executeCommand("ls /");
  }).join();

  trace_request.first->wait(MACH_MSG_TIMEOUT_NONE);

  return status_code;
}

}  // anonymous namespace
}  // namespace shk


int main(int argc, char *argv[]) {
  shk::main(argc, argv);
}
