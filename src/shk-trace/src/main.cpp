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
  auto client_port = openNamedPort(PORT_NAME);
  if (client_port.second == MachOpenPortResult::SUCCESS) {
    return std::make_pair(std::move(client_port.first), true);
  } else {
    return std::make_pair(MachSendRight(), false);
  }
}

bool runTracingServer(std::string *err) {
  int num_cpus = 0;
  if (!getNumCpus(&num_cpus)) {
    *err = "Failed to get number of CPUs";
    return false;
  }

  auto server_port = registerNamedPort(PORT_NAME);
  if (server_port.second == MachPortRegistrationResult::IN_USE) {
    *err = "Mach port already in use. Is there already a server running?";
    return false;
  } else if (server_port.second != MachPortRegistrationResult::SUCCESS) {
    *err = "Failed to bind to mach port.";
    return false;
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
        uintptr_t root_thread_id = request->root_thread_id;
        auto cwd = request->cwd;
        auto trace_writer = std::unique_ptr<PathResolver::Delegate>(
            new TraceWriter(std::move(request)));
        process_tracer.traceProcess(
            pid,
            root_thread_id,
            std::unique_ptr<Tracer::Delegate>(
                new PathResolver(
                    std::move(trace_writer),
                    pid,
                    std::move(cwd))));
      });

  // This is a message to the calling process that indicates that it can expect
  // to be able to make trace requests against it.
  printf("serving\n");
  fflush(stdout);
  close(1);

  tracer.wait(DISPATCH_TIME_FOREVER);

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
  const char *argv[] = { "/bin/sh", "-c", cmd.c_str(), nullptr };
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
  fprintf(
      stderr,
      "Usage:\n"
      "Client mode: shk-trace "
      "[-O/--suicide-when-orphaned] "
      "[-f tracefile] "
      "-c command\n"
      "Server mode: shk-trace "
      "-s/--server"
      "[-O/--suicide-when-orphaned]\n"
      "\n"
      "There can be only one server process at any given time. The client cannot run without a server.\n");
}

void suicideWhenOrphaned() {
  auto ppid = getppid();
  std::thread([ppid] {
    for (;;) {
      if (ppid != getppid()) {
        // Parent process has died! Shutting down.
        exit(1);
      }
      usleep(1000000);
    }
  }).detach();
}

bool runTracingClient(
    const CmdlineOptions &cmdline_options,
    int *status_code,
    std::string *err) {

  auto server_port = tryConnectToServer();
  if (!server_port.second) {
    *err = "Failed to connect to server";
    return false;
  }

  auto trace_fd = openTraceFile(cmdline_options.tracefile);
  if (!trace_fd.second) {
    *err = "Failed to open tracing file: " + std::string(strerror(errno));
    return false;
  }

  auto trace_request = requestTracing(
      std::move(server_port.first),
      std::move(trace_fd.first),
      reinterpret_cast<char *>(
          RAIIHelper<void *, void, free>(getcwd(nullptr, 0)).get()));
  if (trace_request.second != MachOpenPortResult::SUCCESS) {
    *err = "Failed to initiate tracing";
    return false;
  }

  std::thread([&] {
    // Due to a limitation in the tracing information that kdebug provides
    // (when forking, the tracer can't know the parent pid), the traced program
    // (aka this code) creates a thread (which has the same pid as the process
    // that makes the trace request). This triggers tracing to start, and we can
    // then posix_spawn from here.
    *status_code = executeCommand(cmdline_options.command);
  }).join();

  auto wait_result = trace_request.first->wait(3000);
  switch (wait_result) {
  case TraceHandle::WaitResult::SUCCESS:
    return true;
  case TraceHandle::WaitResult::FAILURE:
    *err = "Failed to wait for tracing to finish.";
    return false;
  case TraceHandle::WaitResult::TIMED_OUT:
    *err = "Internal error (deadlocked): Tracing does not seem to finish.";
    return false;
  }
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

  if (cmdline_options.suicide_when_orphaned) {
    suicideWhenOrphaned();
  }

  if (cmdline_options.server) {
    std::string err;
    if (!runTracingServer(&err)) {
      fprintf(stderr, "%s\n", err.c_str());
      return 1;
    }
  } else {
    dropPrivileges();
    int status_code = 1;
    std::string err;
    if (!runTracingClient(cmdline_options, &status_code, &err)) {
      fprintf(stderr, "%s\n", err.c_str());
      return 1;
    }
    return status_code;
  }

  return 0;
}

}  // anonymous namespace
}  // namespace shk


int main(int argc, char *argv[]) {
  return shk::main(argc, argv);
}
