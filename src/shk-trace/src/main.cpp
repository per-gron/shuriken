#include <errno.h>
#include <libc.h>
#include <spawn.h>
#include <string.h>
#include <thread>

#include "cmdline_options.h"
#include "event_consolidator.h"
#include "kdebug_controller.h"
#include "named_mach_port.h"
#include "path_resolver.h"
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

class PathResolverDelegate : public PathResolver::Delegate {
 public:
  PathResolverDelegate(std::unique_ptr<TracingServer::TraceRequest> &&request)
      : _request(std::move(request)) {}

  virtual ~PathResolverDelegate() {
    auto events = _consolidator.getConsolidatedEventsAndReset();
    for (const auto &event : events) {
      write(eventTypeToString(event.first) + (" " + event.second) + "\n");
    }
    // TODO(peck): Do something about quitting the tracing server as well.
  }

  virtual void fileEvent(
      uintptr_t thread_id,
      EventType type,
      int at_fd,
      std::string &&path) override {
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
                nullptr/*new PathResolverDelegate(std::move(request))*/));
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
  const char *argv[] = { "sh", "-c", cmd.c_str(), nullptr };
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

void printUsage() {
  fprintf(stderr, "usage: shk-trace -f tracefile -c command\n");
}

int main(int argc, char *argv[]) {
  auto cmdline_options = CmdlineOptions::parse(argc, argv);
  if (cmdline_options.result == CmdlineOptions::Result::VERSION) {
    // TODO(peck): Print version
  } else if (cmdline_options.result != CmdlineOptions::Result::SUCCESS) {
    printUsage();
    return 1;
  }

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
        auto cwd = request->cwd;
        process_tracer.traceProcess(
            pid,
            std::unique_ptr<Tracer::Delegate>(
                new PathResolver(
                    std::unique_ptr<PathResolver::Delegate>(
                        new PathResolverDelegate(std::move(request))),
                    pid,
                    std::move(cwd))));
      });

  //auto mach_port = connectToServer();
  /*if (!mach_port.second) {
    TODO(peck): Do error checking here
    return 1;
  }*/

  dropPrivileges();

  auto trace_fd = openTraceFile(cmdline_options.tracefile);
  if (!trace_fd.second) {
    fprintf(stderr, "Failed to open tracing file: %s\n", strerror(errno));
    return 1;
  }

  auto trace_request = requestTracing(
      std::move(port_pair.second),
      std::move(trace_fd.first),
      reinterpret_cast<char *>(
          RAIIHelper<void *, void, free>(getcwd(nullptr, 0)).get()));
  if (trace_request.second != MachOpenPortResult::SUCCESS) {
    fprintf(stderr, "Failed to initiate tracing\n");
    return 1;
  }

  int status_code;
  std::thread([&] {
    // Due to a limitation in the tracing information that kdebug provides
    // (when forking, the tracer can't know the parent pid), the traced program
    // (aka this code) creates a thread (which has the same pid as the process
    // that makes the trace request). This triggers tracing to start, and we can
    // then posix_spawn from here.
    status_code = executeCommand(cmdline_options.command);
  }).join();

  // TODO(peck): If the timeout provided here is MACH_MSG_TIMEOUT_NONE, the
  // process sometimes just stalls. I don't know why. This probably needs to be
  // fixed.
  auto wait_result = trace_request.first->wait(3000);
  switch (wait_result) {
  case TraceHandle::WaitResult::SUCCESS:
    return status_code;
  case TraceHandle::WaitResult::FAILURE:
    fprintf(stderr, "Failed to wait for tracing to finish.\n");
    return 1;
  case TraceHandle::WaitResult::TIMED_OUT:
    fprintf(
        stderr,
        "Internal error (deadlocked): Tracing does not seem to finish.\n");
    return 1;
  }
}

}  // anonymous namespace
}  // namespace shk


int main(int argc, char *argv[]) {
  return shk::main(argc, argv);
}
