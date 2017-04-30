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

#include <errno.h>
#include <libc.h>
#include <spawn.h>
#include <string.h>
#include <thread>

#include "cmdline_options.h"
#include "debug_capture_log.h"
#include "event_consolidator.h"
#include "kdebug_controller.h"
#include "kdebug_pump.h"
#include "named_mach_port.h"
#include "path_resolver.h"
#include "process_tracer.h"
#include "to_json.h"
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

std::pair<FileDescriptor, bool> openTraceFile(const std::string &path) {
  int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
  return std::make_pair(FileDescriptor(fd), fd != -1);
}

void processTraceRequest(
    std::unique_ptr<TracingServer::TraceRequest> &&request,
    ProcessTracer *process_tracer) {
  pid_t pid = request->pid_to_trace;
  uintptr_t root_thread_id = request->root_thread_id;
  auto cwd = request->cwd;
  auto trace_writer = std::unique_ptr<PathResolver::Delegate>(
      new TraceWriter(std::move(request)));
  process_tracer->traceProcess(
      pid,
      root_thread_id,
      std::unique_ptr<Tracer::Delegate>(
          new PathResolver(
              std::move(trace_writer),
              pid,
              std::move(cwd))));
}

bool runTracingServer(const std::string &capture_file, std::string *err) {
  int num_cpus = 0;
  if (!getNumCpus(&num_cpus)) {
    *err = "Failed to get number of CPUs";
    return false;
  }

  std::unique_ptr<DebugCaptureLog> capture_log;
  if (!capture_file.empty()) {
    auto capture_log_fd = openTraceFile(capture_file);
    if (!capture_log_fd.second) {
      *err = "Failed to open capture log file: " + std::string(strerror(errno));
      return false;
    }

    capture_log.reset(new DebugCaptureLog(std::move(capture_log_fd.first)));
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

  Tracer tracer(process_tracer);

  KdebugPump kdebug_pump(
      num_cpus,
      kdebug_ctrl.get(),
      [&tracer, &capture_log](const kd_buf *begin, const kd_buf *end) {
        if (capture_log) {
          capture_log->writeKdBufs(begin, end);
        }

        return tracer.parseBuffer(begin, end);
      });

  kdebug_pump.start(queue.get());

  auto tracing_server = makeTracingServer(
      queue.get(),
      std::move(server_port.first),
      [&](std::unique_ptr<TracingServer::TraceRequest> &&request) {
        if (capture_log) {
          capture_log->writeTraceRequest(*request);
        }

        processTraceRequest(std::move(request), &process_tracer);
      });

  // This is a message to the calling process that indicates that it can expect
  // to be able to make trace requests against it.
  printf("serving\n");
  fflush(stdout);
  close(1);

  kdebug_pump.wait(DISPATCH_TIME_FOREVER);

  return true;
}

bool processReplayFile(const std::string &capture_log_file, std::string *err) {
  auto capture_log_fd = FileDescriptor(
      open(capture_log_file.c_str(), O_RDONLY));
  if (capture_log_fd.get() == -1) {
    *err = "Failed to open capture log file: " + std::string(strerror(errno));
    return false;
  }

  ProcessTracer process_tracer;
  Tracer tracer(process_tracer);

  return DebugCaptureLog::parse(
      capture_log_fd,
      [&](std::unique_ptr<TracingServer::TraceRequest> &&trace_request) {
        processTraceRequest(std::move(trace_request), &process_tracer);
      },
      [&](const kd_buf *begin, const kd_buf *end) {
        tracer.parseBuffer(begin, end);
      },
      err);
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

void printUsage() {
  fprintf(
      stderr,
      "Usage:\n"
      "Client mode: shk-trace "
          "[-O/--suicide-when-orphaned] "
          "[-j/--json] "
          "[-f tracefile] "
          "-c command\n"
      "Server mode: shk-trace "
          "-s/--server "
          "[-C/--capture capture-file] "
          "[-O/--suicide-when-orphaned]\n"
      "Replay mode: shk-trace "
          "-r/--replay capture-file\n\n"
      "There can be only one server process at any given time. The client "
          "cannot run without a server.\n");
}

void suicideWhenOrphaned() {
  auto ppid = getppid();
  std::thread([ppid] {
    for (;;) {
      if (ppid != getppid()) {
        // Parent process has died! Shutting down.
        exit(1);
      }
      usleep(100000);
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
    if (cmdline_options.json) {
      return convertOutputToJson(cmdline_options.tracefile, err);
    } else {
      return true;
    }
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

  if (!cmdline_options.replay.empty()) {
    std::string err;
    if (!processReplayFile(cmdline_options.replay, &err)) {
      fprintf(stderr, "Failed to replay: %s\n", err.c_str());
      return 1;
    }
    return 0;
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

  if (cmdline_options.server) {
    // Suicide-when-orphaned works only on the server, because for processes
    // that are to be traced, there is a chance that the tracer will select
    // the suicide-when-orphaned thread as the thread to trace, which causes
    // tracing to deadlock.
    if (cmdline_options.suicide_when_orphaned) {
      suicideWhenOrphaned();
    }

    std::string err;
    if (!runTracingServer(cmdline_options.capture, &err)) {
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
