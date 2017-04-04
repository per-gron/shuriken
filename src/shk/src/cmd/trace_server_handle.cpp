#include "cmd/trace_server_handle.h"

#include <errno.h>
#include <mach-o/dyld.h>
#include <libgen.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <vector>

#include <util/file_descriptor.h>

extern char **environ;

namespace shk {
namespace {

using PosixSpawnFileActions = RAIIHelper<posix_spawn_file_actions_t *, int, posix_spawn_file_actions_destroy>;

class RealTraceServerHandle : public TraceServerHandle {
 public:
  RealTraceServerHandle(const std::string &shk_trace_command)
      : _executable_path(computeExecutablePath(shk_trace_command)) {}

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
      const std::string &shk_trace_command) {
    return std::unique_ptr<TraceServerHandle>(new RealTraceServerHandle(
        shk_trace_command));
  }

  virtual const std::string &getShkTracePath(std::string *err) override {
    static const std::string empty_string;

    if (!_pid) {
      if (!spawnServer(err)) {
        return empty_string;
      }
    }

    return _executable_path;
  }

 private:
  bool spawnServer(std::string *err) {
    int stdout_pipe[2];
    if (pipe(stdout_pipe)) {
      *err = "pipe() failed";
      return false;
    }

    FileDescriptor stdout(stdout_pipe[0]);
    FileDescriptor stdout_child(stdout_pipe[1]);

    posix_spawn_file_actions_t raw_actions;
    posix_spawn_file_actions_init(&raw_actions);
    PosixSpawnFileActions actions(&raw_actions);
    posix_spawn_file_actions_addclose(actions.get(), stdout.get());
    posix_spawn_file_actions_adddup2(actions.get(), stdout_child.get(), 1);
    posix_spawn_file_actions_addclose(actions.get(), stdout_child.get());

    const char *argv[] = {
        _executable_path.c_str(),
        "-s",  // server mode
        "-O",  // suicide-when-orphaned
        nullptr };

    if (posix_spawn(
            &_pid,
            _executable_path.c_str(),
            actions.get(),
            nullptr,
            const_cast<char **>(argv),
            environ)) {
      _pid = 0;
      *err = std::string("posix_spawn() failed");
      return false;
    }

    stdout_child.reset();

    static const std::string kExpectedMessage = "serving\n";
    char buf[64];
    bzero(buf, sizeof(buf));
    int bytes_read = read(stdout.get(), buf, sizeof(buf));
    if (bytes_read == -1) {
      *err = std::string("read(): ") + strerror(errno);
      return false;
    }
    if (strncmp(buf, kExpectedMessage.data(), sizeof(buf)) != 0) {
      *err = "did not see expected acknowledgement message";
      return false;
    }

    return true;
  }

  static std::string computeExecutablePath(
      const std::string &shk_trace_command) {
    if (!shk_trace_command.empty() && shk_trace_command[0] == '/') {
      return shk_trace_command;
    }

    uint32_t bufsize = 0;
    _NSGetExecutablePath(nullptr, &bufsize);
    std::vector<char> executable_path(bufsize);
    _NSGetExecutablePath(executable_path.data(), &bufsize);

    // +2: null pointer and potential "." if empty input, just to be safe
    std::vector<char> dirname_buf(executable_path.size() + 2);
    const char * const dirname = dirname_r(
        executable_path.data(), dirname_buf.data());

    return dirname + ("/" + shk_trace_command);
  }

  const std::string _executable_path;
  pid_t _pid = 0;
};

}  // anonymous namespace

std::unique_ptr<TraceServerHandle> TraceServerHandle::open(
    const std::string &shk_trace_path) {
  return RealTraceServerHandle::open(shk_trace_path);
}

}  // namespace shk
