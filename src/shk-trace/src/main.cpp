#include <errno.h>
#include <libc.h>
#include <spawn.h>
#include <string.h>
#include <thread>

#include "cmdline_options.h"
#include "daemon.h"
#include "event_consolidator.h"
#include "kdebug_controller.h"
#include "named_mach_port.h"
#include "path_resolver.h"
#include "process_tracer.h"
#include "trace_writer.h"
#include "tracer.h"
#include "tracing_server.h"

extern "C" int reexec_to_match_kernel();
extern "C" char **environ;

namespace shk {
namespace {

bool getNumCpus(int *out) {
  int num_cpus;
  size_t len = sizeof(num_cpus);
  static int name[] = { CTL_HW, HW_NCPU, 0 };
  if (sysctl(name, 2, &num_cpus, &len, nullptr, 0) < 0) {
    return false;
  }
  *out = num_cpus;
  return true;
}

static const std::string PORT_NAME = "com.pereckerdal.shktrace";

std::pair<MachSendRight, bool> tryConnectToServer() {
  for (int attempts = 0; attempts < 100; attempts++) {
    auto client_port = openNamedPort(PORT_NAME);
    if (client_port.second == MachOpenPortResult::SUCCESS) {
      return std::make_pair(std::move(client_port.first), true);
    } else if (client_port.second != MachOpenPortResult::NOT_FOUND) {
      return std::make_pair(MachSendRight(), false);
    }

    usleep(2000);
  }

  return std::make_pair(MachSendRight(), false);
}

bool tryForkAndSpawnTracingServer(std::string *err) {
  int num_cpus = 0;
  if (!getNumCpus(&num_cpus)) {
    *err = "Failed to get number of CPUs";
    return false;
  }

  daemon(DaemonConfig(), [num_cpus] {
    auto server_port = registerNamedPort(PORT_NAME);
    if (server_port.second != MachPortRegistrationResult::SUCCESS) {
      // TODO(peck): handle failure
      fprintf(stderr, "internal error\n");
      abort();
    }

    DispatchQueue queue(dispatch_queue_create(
        "shk-trace-server",
        DISPATCH_QUEUE_SERIAL));

    auto kdebug_ctrl = makeKdebugController();

    ProcessTracer process_tracer;

    Tracer tracer(
        num_cpus,
        *kdebug_ctrl,
        process_tracer);
    tracer.start(queue.get());

    auto tracing_server = makeTracingServer(
        queue.get(),
        std::move(server_port.first),
        [&](std::unique_ptr<TracingServer::TraceRequest> &&request) {
          pid_t pid = request->pid_to_trace;
          auto cwd = request->cwd;
          auto trace_writer = std::unique_ptr<PathResolver::Delegate>(
              new TraceWriter(std::move(request)));
          process_tracer.traceProcess(
              pid,
              std::unique_ptr<Tracer::Delegate>(
                  new PathResolver(
                      std::move(trace_writer),
                      pid,
                      std::move(cwd))));
        });

    tracer.wait(DISPATCH_TIME_FOREVER);
  });

  return true;
}

void dropPrivileges() {
  uid_t newuid = getuid();
  uid_t olduid = geteuid();

  if (newuid != olduid) {
    seteuid(newuid);
    if (setuid(newuid) == -1) {
      fprintf(stderr, "Failed to drop privileges\n");
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
  fprintf(stderr, "usage: shk-trace [-f tracefile] -c command\n");
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

  std::string err;
  if (!tryForkAndSpawnTracingServer(&err)) {
    fprintf(stderr, "%s\n", err.c_str());
    return 1;
  }

  dropPrivileges();

  auto server_port = tryConnectToServer();
  if (!server_port.second) {
    fprintf(stderr, "Failed to connect to server\n");
    return 1;
  }

  auto trace_fd = openTraceFile(cmdline_options.tracefile);
  if (!trace_fd.second) {
    fprintf(stderr, "Failed to open tracing file: %s\n", strerror(errno));
    return 1;
  }

  auto trace_request = requestTracing(
      std::move(server_port.first),
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
