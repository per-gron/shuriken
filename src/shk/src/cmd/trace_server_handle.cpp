#include "cmd/trace_server_handle.h"

#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <sys/wait.h>

#include <util/file_descriptor.h>

extern char **environ;

namespace shk {
namespace {

using PosixSpawnFileActions = RAIIHelper<posix_spawn_file_actions_t *, int, posix_spawn_file_actions_destroy>;

class RealTraceServerHandle : public TraceServerHandle {
 public:
  ~RealTraceServerHandle() {
    if (_pid) {
      kill(_pid, SIGTERM);

      // Call waitpid on _pid, to avoid zombie processes
      int status;
      if (waitpid(_pid, &status, 0) == -1) {
        fprintf(stderr, "Failed to wait for child process: %s\n", strerror(errno));
        abort();
      }
    }
  }

  static std::unique_ptr<TraceServerHandle> open(
      const std::string &shk_trace_command,
      std::string *err) {
    int stdout_pipe[2];
    if (pipe(stdout_pipe)) {
      *err = "pipe() failed";
      return nullptr;
    }

    FileDescriptor stdout(stdout_pipe[0]);
    FileDescriptor stdout_child(stdout_pipe[1]);

    posix_spawn_file_actions_t raw_actions;
    PosixSpawnFileActions actions(&raw_actions);
    posix_spawn_file_actions_init(actions.get());
    posix_spawn_file_actions_addclose(actions.get(), stdout.get());
    posix_spawn_file_actions_adddup2(actions.get(), stdout_child.get(), 1);
    posix_spawn_file_actions_addclose(actions.get(), stdout_child.get());

    const char *argv[] = {
        "/bin/sh",
        "-c",
        shk_trace_command.c_str(),
        nullptr };

    std::unique_ptr<TraceServerHandle> handle(new RealTraceServerHandle());

    if (posix_spawn(
            &static_cast<RealTraceServerHandle *>(handle.get())->_pid,
            "/bin/sh",
            actions.get(),
            nullptr,
            const_cast<char **>(argv),
            environ)) {
      static_cast<RealTraceServerHandle *>(handle.get())->_pid = 0;
      *err = std::string("posix_spawn() failed");
      return nullptr;
    }

    stdout_child.reset();

    static const std::string kExpectedMessage = "serving\n";
    char buf[64];
    bzero(buf, sizeof(buf));
    int bytes_read = read(stdout.get(), buf, sizeof(buf));
    if (bytes_read == -1) {
      *err = std::string("read(): ") + strerror(errno);
      return nullptr;
    }
    if (strncmp(buf, kExpectedMessage.data(), sizeof(buf)) != 0) {
      *err = "did not see expected acknowledgement message";
      return nullptr;
    }

    return handle;
  }

 private:
  pid_t _pid = 0;
};

}  // anonymous namespace

std::unique_ptr<TraceServerHandle> TraceServerHandle::open(
    const std::string &shk_trace_path,
    std::string *err) {
  return RealTraceServerHandle::open(shk_trace_path, err);
}

}  // namespace shk
