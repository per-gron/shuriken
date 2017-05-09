// Copyright 2012 Google Inc. All Rights Reserved.
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

#include "cmd/real_command_runner.h"

#include <string>
#include <vector>
#include <queue>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>

#include "manifest/step.h"
#include "nullterminated_string.h"
#include "util.h"

extern char **environ;

namespace shk {

enum class UseConsole {
  NO,
  YES,
};

/**
 * Subprocess wraps a single async subprocess.  It is entirely
 * passive: it expects the caller to notify it when its fds are ready
 * for reading, as well as call finish() to reap the child once done()
 * is true.
 */
class Subprocess {
 public:
  ~Subprocess();

  bool done() const;

 private:
  Subprocess(
      const CommandRunner::Callback &callback,
      UseConsole use_console,
      bool generator);

  void finish(bool invoke_callback);
  void start(class SubprocessSet *set, nt_string_view command);
  void onPipeReady();

  const CommandRunner::Callback _callback;
  std::string _buf;

  int _fd = -1;
  pid_t _pid = -1;
  UseConsole _use_console;
  bool _generator = false;

  friend class SubprocessSet;
};

class SubprocessSet : public CommandRunner {
 public:
  SubprocessSet();
  ~SubprocessSet();

  void invoke(
      nt_string_view command,
      Step step,
      const Callback &callback) override;
  bool runCommands() override;
  void clear();

  size_t size() const override {
    return _running.size() + _finished.size();
  }

  bool canRunMore() const override {
    return true;
  }

 private:
  std::vector<std::unique_ptr<Subprocess>> _running;
  std::vector<std::unique_ptr<Subprocess>> _finished;

  static void setInterruptedFlag(int signum);
  static void handlePendingInterruption();
  /**
   * Store the signal number that causes the interruption.
   * 0 if not interruption.
   */
  static int _interrupted;

  static bool isInterrupted() { return _interrupted != 0; }

  struct sigaction _old_int_act;
  struct sigaction _old_hup_act;
  struct sigaction _old_term_act;
  sigset_t _old_mask;

  friend class Subprocess;
};

Subprocess::Subprocess(
    const CommandRunner::Callback &callback,
    UseConsole use_console,
    bool generator)
    : _callback(callback),
      _use_console(use_console),
      _generator(generator) {}

Subprocess::~Subprocess() {
  if (_fd >= 0) {
    close(_fd);
  }
  // Reap child if forgotten.
  if (_pid != -1) {
    finish(false);
  }
}

void Subprocess::start(SubprocessSet *set, nt_string_view command) {
  int output_pipe[2];
  if (pipe(output_pipe) < 0) {
    fatal("pipe: %s", strerror(errno));
  }
  _fd = output_pipe[0];
#if !defined(USE_PPOLL)
  // If available, we use ppoll in runCommands(); otherwise we use pselect
  // and so must avoid overly-large FDs.
  if (_fd >= static_cast<int>(FD_SETSIZE)) {
    fatal("pipe: %s", strerror(EMFILE));
  }
#endif  // !defined(USE_PPOLL)
  setCloseOnExec(_fd);

  posix_spawn_file_actions_t action;
  if (posix_spawn_file_actions_init(&action) != 0) {
    fatal("posix_spawn_file_actions_init: %s", strerror(errno));
  }

  if (posix_spawn_file_actions_addclose(&action, output_pipe[0]) != 0) {
    fatal("posix_spawn_file_actions_addclose: %s", strerror(errno));
  }

  posix_spawnattr_t attr;
  if (posix_spawnattr_init(&attr) != 0) {
    fatal("posix_spawnattr_init: %s", strerror(errno));
  }

  short flags = 0;

  flags |= POSIX_SPAWN_SETSIGMASK;
  if (posix_spawnattr_setsigmask(&attr, &set->_old_mask) != 0) {
    fatal("posix_spawnattr_setsigmask: %s", strerror(errno));
  }
  // Signals which are set to be caught in the calling process image are set to
  // default action in the new process image, so no explicit
  // POSIX_SPAWN_SETSIGDEF parameter is needed.

  if (_use_console == UseConsole::NO) {
    // Put the child in its own process group, so ctrl-c won't reach it.
    flags |= POSIX_SPAWN_SETPGROUP;
    // No need to posix_spawnattr_setpgroup(&attr, 0), it's the default.

    // Open /dev/null over stdin.
    if (posix_spawn_file_actions_addopen(&action, 0, "/dev/null", O_RDONLY,
                                         0) != 0) {
      fatal("posix_spawn_file_actions_addopen: %s", strerror(errno));
    }

    if (posix_spawn_file_actions_adddup2(&action, output_pipe[1], 1) != 0) {
      fatal("posix_spawn_file_actions_adddup2: %s", strerror(errno));
    }
    if (posix_spawn_file_actions_adddup2(&action, output_pipe[1], 2) != 0) {
      fatal("posix_spawn_file_actions_adddup2: %s", strerror(errno));
    }
    if (posix_spawn_file_actions_addclose(&action, output_pipe[1]) != 0) {
      fatal("posix_spawn_file_actions_addclose: %s", strerror(errno));
    }
    // In the console case, output_pipe is still inherited by the child and
    // closed when the subprocess finishes, which then notifies ninja.
  }
#ifdef POSIX_SPAWN_USEVFORK
  flags |= POSIX_SPAWN_USEVFORK;
#endif

  if (posix_spawnattr_setflags(&attr, flags) != 0) {
    fatal("posix_spawnattr_setflags: %s", strerror(errno));
  }

  const char* spawned_args[] = {
      "/bin/sh",
      "-c",
      NullterminatedString(command).c_str(),
      nullptr };

  const char *const_child_environ[] = {
      "__CF_USER_TEXT_ENCODING=0x1F5:0x0:0x0",
      "LC_CTYPE=UTF-8",
      nullptr };
  // When a generator is invoked for the first time, it's not invoked with
  // Shuriken, so it won't be shielded from environment variables. To be
  // consistent with what is required from a generator command, allow it to see
  // the environment.
  //
  // From a safety/correctness perspective this is fine, because generator rules
  // are not cached; if they generate different manifests on different machines
  // that's perfectly fine.
  char **child_environ = _generator ?
      environ :
      const_cast<char **>(const_child_environ);

  if (posix_spawn(
          &_pid,
          "/bin/sh",
          &action,
          &attr,
          const_cast<char**>(spawned_args),
          child_environ) != 0) {
    fatal("posix_spawn: %s", strerror(errno));
  }

  if (posix_spawnattr_destroy(&attr) != 0) {
    fatal("posix_spawnattr_destroy: %s", strerror(errno));
  }
  if (posix_spawn_file_actions_destroy(&action) != 0) {
    fatal("posix_spawn_file_actions_destroy: %s", strerror(errno));
  }

  close(output_pipe[1]);
}

void Subprocess::onPipeReady() {
  char buf[4 << 10];
  ssize_t len = read(_fd, buf, sizeof(buf));
  if (len > 0) {
    _buf.append(buf, len);
  } else if (len < 0) {
    if (errno == EINTR) {
      return;
    } else {
      fatal("read: %s", strerror(errno));
    }
  } else {
    close(_fd);
    _fd = -1;
  }
}

namespace {

ExitStatus computeExitStatus(int status) {
  if (WIFEXITED(status)) {
    int exit = WEXITSTATUS(status);
    if (exit == 0) {
      return ExitStatus::SUCCESS;
    }
  } else if (WIFSIGNALED(status)) {
    if (WTERMSIG(status) == SIGINT ||
        WTERMSIG(status) == SIGTERM ||
        WTERMSIG(status) == SIGHUP) {
      return ExitStatus::INTERRUPTED;
    }
  }
  return ExitStatus::FAILURE;
}

}  // anonymous namespace

void Subprocess::finish(bool invoke_callback) {
  assert(_pid != -1);
  int status;
  while (waitpid(_pid, &status, 0) < 0) {
    if (errno != EINTR) {
      fatal("waitpid(%d): %s", _pid, strerror(errno));
    }
  }
  _pid = -1;

  const auto exit_status = computeExitStatus(status);

  if (invoke_callback) {
    CommandRunner::Result result;
    result.exit_status = exit_status;
    result.output = std::move(_buf);
    _callback(std::move(result));
  }
}

bool Subprocess::done() const {
  return _fd == -1;
}

int SubprocessSet::_interrupted;

void SubprocessSet::setInterruptedFlag(int signum) {
  _interrupted = signum;
}

void SubprocessSet::handlePendingInterruption() {
  sigset_t pending;
  sigemptyset(&pending);
  if (sigpending(&pending) == -1) {
    perror("shk: sigpending");
    return;
  }
  if (sigismember(&pending, SIGINT)) {
    _interrupted = SIGINT;
  } else if (sigismember(&pending, SIGTERM)) {
    _interrupted = SIGTERM;
  } else if (sigismember(&pending, SIGHUP)) {
    _interrupted = SIGHUP;
  }
}

SubprocessSet::SubprocessSet() {
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGINT);
  sigaddset(&set, SIGTERM);
  sigaddset(&set, SIGHUP);
  if (sigprocmask(SIG_BLOCK, &set, &_old_mask) < 0) {
    fatal("sigprocmask: %s", strerror(errno));
  }

  struct sigaction act;
  memset(&act, 0, sizeof(act));
  act.sa_handler = setInterruptedFlag;
  if (sigaction(SIGINT, &act, &_old_int_act) < 0) {
    fatal("sigaction: %s", strerror(errno));
  }
  if (sigaction(SIGTERM, &act, &_old_term_act) < 0) {
    fatal("sigaction: %s", strerror(errno));
  }
  if (sigaction(SIGHUP, &act, &_old_hup_act) < 0) {
    fatal("sigaction: %s", strerror(errno));
  }
}

SubprocessSet::~SubprocessSet() {
  clear();

  if (sigaction(SIGINT, &_old_int_act, 0) < 0) {
    fatal("sigaction: %s", strerror(errno));
  }
  if (sigaction(SIGTERM, &_old_term_act, 0) < 0) {
    fatal("sigaction: %s", strerror(errno));
  }
  if (sigaction(SIGHUP, &_old_hup_act, 0) < 0) {
    fatal("sigaction: %s", strerror(errno));
  }
  if (sigprocmask(SIG_SETMASK, &_old_mask, 0) < 0) {
    fatal("sigprocmask: %s", strerror(errno));
  }
}

void SubprocessSet::invoke(
    nt_string_view command,
    Step step,
    const CommandRunner::Callback &callback) {
  auto use_console =
      isConsolePool(step.poolName()) ? UseConsole::YES : UseConsole::NO;
  auto subprocess = std::unique_ptr<Subprocess>(
      new Subprocess(callback, use_console, step.generator()));
  subprocess->start(this, command);
  _running.push_back(std::move(subprocess));
}

#ifdef USE_PPOLL
bool SubprocessSet::runCommands() {
  vector<pollfd> fds;
  nfds_t nfds = 0;

  decltype(_finished) finished;
  // Need to clear _finished before invoking callbacks, to make size() report
  // the right thing if that is called from a callback.
  finished.swap(_finished);
  for (const auto &subprocess : finished) {
    subprocess->finish(true);
  }

  for (const auto &subprocess : _running) {
    int fd = subprocess->_fd;
    if (fd < 0) {
      continue;
    }
    pollfd pfd = { fd, POLLIN | POLLPRI, 0 };
    fds.push_back(pfd);
    ++nfds;
  }

  if (nfds == 0) {
    return false;
  }

  _interrupted = 0;
  int ret = ppoll(&fds.front(), nfds, NULL, &_old_mask);
  if (ret == -1) {
    if (errno != EINTR) {
      perror("shk: ppoll");
      return false;
    }
    return isInterrupted();
  }

  handlePendingInterruption();
  if (isInterrupted()) {
    return true;
  }

  nfds_t cur_nfd = 0;
  for (auto i = _running.begin(); i != _running.end(); ) {
    int fd = (*i)->_fd;
    if (fd < 0) {
      continue;
    }
    assert(fd == fds[cur_nfd].fd);
    if (fds[cur_nfd++].revents) {
      (*i)->onPipeReady();
      if ((*i)->done()) {
        _finished.push_back(std::move(*i));
        i = _running.erase(i);
        continue;
      }
    }
  }

  return isInterrupted();
}

#else  // USE_PPOLL
bool SubprocessSet::runCommands() {
  fd_set set;
  int nfds = 0;
  FD_ZERO(&set);

  decltype(_finished) finished;
  // Need to clear _finished before invoking callbacks, to make size() report
  // the right thing if that is called from a callback.
  finished.swap(_finished);
  for (const auto &subprocess : finished) {
    subprocess->finish(true);
  }

  for (const auto &subprocess : _running) {
    int fd = subprocess->_fd;
    if (fd >= 0) {
      FD_SET(fd, &set);
      if (nfds < fd + 1) {
        nfds = fd + 1;
      }
    }
  }

  if (nfds == 0) {
    return false;
  }

  _interrupted = 0;
  int ret = pselect(nfds, &set, 0, 0, 0, &_old_mask);
  if (ret == -1) {
    if (errno != EINTR) {
      perror("shk: pselect");
      return false;
    }
    return isInterrupted();
  }

  handlePendingInterruption();
  if (isInterrupted()) {
    return true;
  }

  for (auto i = _running.begin(); i != _running.end(); ) {
    int fd = (*i)->_fd;
    if (fd >= 0 && FD_ISSET(fd, &set)) {
      (*i)->onPipeReady();
      if ((*i)->done()) {
        _finished.push_back(std::move(*i));
        i = _running.erase(i);
        continue;
      }
    }
    ++i;
  }

  return isInterrupted();
}
#endif  // USE_PPOLL

void SubprocessSet::clear() {
  for (const auto &subprocess : _running) {
    // Since the foreground process is in our process group, it will receive
    // the interruption signal (i.e. SIGINT or SIGTERM) at the same time as us.
    if (subprocess->_use_console == UseConsole::NO) {
      kill(-subprocess->_pid, _interrupted);
    }
  }
  _running.clear();
}

std::unique_ptr<CommandRunner> makeRealCommandRunner() {
  return std::unique_ptr<CommandRunner>(
      new SubprocessSet());
}

}  // namespace shk
