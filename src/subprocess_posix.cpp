// Copyright 2012 Google Inc. All Rights Reserved.
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

#include "subprocess.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>

#include "util.h"

namespace shk {

Subprocess::Subprocess(bool use_console)
    : _use_console(use_console) {}

Subprocess::~Subprocess() {
  if (_fd >= 0) {
    close(_fd);
  }
  // Reap child if forgotten.
  if (_pid != -1) {
    finish();
  }
}

void Subprocess::start(SubprocessSet *set, const std::string &command) {
  int output_pipe[2];
  if (pipe(output_pipe) < 0) {
    fatal("pipe: %s", strerror(errno));
  }
  _fd = output_pipe[0];
#if !defined(USE_PPOLL)
  // If available, we use ppoll in DoWork(); otherwise we use pselect
  // and so must avoid overly-large FDs.
  if (_fd >= static_cast<int>(FD_SETSIZE)) {
    fatal("pipe: %s", strerror(EMFILE));
  }
#endif  // !USE_PPOLL
  setCloseOnExec(_fd);

  _pid = fork();
  if (_pid < 0) {
    fatal("fork: %s", strerror(errno));
  }

  if (_pid == 0) {
    close(output_pipe[0]);

    // Track which fd we use to report errors on.
    int error_pipe = output_pipe[1];
    do {
      if (sigaction(SIGINT, &set->_old_int_act, 0) < 0) {
        break;
      }
      if (sigaction(SIGTERM, &set->_old_term_act, 0) < 0) {
        break;
      }
      if (sigprocmask(SIG_SETMASK, &set->_old_mask, 0) < 0) {
        break;
      }

      if (!_use_console) {
        // Put the child in its own session and process group. It will be
        // detached from the current terminal and ctrl-c won't reach it.
        // Since this process was just forked, it is not a process group leader
        // and setsid() will succeed.
        if (setsid() < 0) {
          break;
        }

        // Open /dev/null over stdin.
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull < 0) {
          break;
        }
        if (dup2(devnull, 0) < 0) {
          break;
        }
        close(devnull);

        if (dup2(output_pipe[1], 1) < 0 ||
            dup2(output_pipe[1], 2) < 0) {
          break;
        }

        // Now can use stderr for errors.
        error_pipe = 2;
        close(output_pipe[1]);
      }
      // In the console case, output_pipe is still inherited by the child and
      // closed when the subprocess finishes, which then notifies ninja.

      execl("/bin/sh", "/bin/sh", "-c", command.c_str(), (char *) NULL);
    } while (false);

    // If we get here, something went wrong; the execl should have
    // replaced us.
    char* err = strerror(errno);
    if (write(error_pipe, err, strlen(err)) < 0) {
      // If the write fails, there's nothing we can do.
      // But this block seems necessary to silence the warning.
    }
    _exit(1);
  }

  close(output_pipe[1]);
}

void Subprocess::onPipeReady() {
  char buf[4 << 10];
  ssize_t len = read(_fd, buf, sizeof(buf));
  if (len > 0) {
    _buf.append(buf, len);
  } else {
    if (len < 0) {
      fatal("read: %s", strerror(errno));
    }
    close(_fd);
    _fd = -1;
  }
}

ExitStatus Subprocess::finish() {
  assert(_pid != -1);
  int status;
  if (waitpid(_pid, &status, 0) < 0) {
    fatal("waitpid(%d): %s", _pid, strerror(errno));
  }
  _pid = -1;

  if (WIFEXITED(status)) {
    int exit = WEXITSTATUS(status);
    if (exit == 0) {
      return ExitStatus::SUCCESS;
    }
  } else if (WIFSIGNALED(status)) {
    if (WTERMSIG(status) == SIGINT || WTERMSIG(status) == SIGTERM) {
      return ExitStatus::INTERRUPTED;
    }
  }
  return ExitStatus::FAILURE;
}

bool Subprocess::done() const {
  return _fd == -1;
}

const std::string &Subprocess::getOutput() const {
  return _buf;
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
  if (sigismember(&pending, SIGINT))
    _interrupted = SIGINT;
  else if (sigismember(&pending, SIGTERM))
    _interrupted = SIGTERM;
}

SubprocessSet::SubprocessSet() {
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGINT);
  sigaddset(&set, SIGTERM);
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
}

SubprocessSet::~SubprocessSet() {
  clear();

  if (sigaction(SIGINT, &_old_int_act, 0) < 0) {
    fatal("sigaction: %s", strerror(errno));
  }
  if (sigaction(SIGTERM, &_old_term_act, 0) < 0) {
    fatal("sigaction: %s", strerror(errno));
  }
  if (sigprocmask(SIG_SETMASK, &_old_mask, 0) < 0) {
    fatal("sigprocmask: %s", strerror(errno));
  }
}

Subprocess *SubprocessSet::add(const std::string &command, bool use_console) {
  Subprocess *subprocess = new Subprocess(use_console);
  subprocess->start(this, command);
  _running.push_back(subprocess);
  return subprocess;
}

#ifdef USE_PPOLL
bool SubprocessSet::doWork() {
  vector<pollfd> fds;
  nfds_t nfds = 0;

  for (const auto *subprocess : _running) {
    int fd = subprocess->_fd;
    if (fd < 0) {
      continue;
    }
    pollfd pfd = { fd, POLLIN | POLLPRI, 0 };
    fds.push_back(pfd);
    ++nfds;
  }

  _interrupted = 0;
  int ret = ppoll(&fds.front(), nfds, NULL, &old_mask_);
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
  for (const auto *subprocess : _running) {
    int fd = subprocess->_fd;
    if (fd < 0) {
      continue;
    }
    assert(fd == fds[cur_nfd].fd);
    if (fds[cur_nfd++].revents) {
      subprocess->onPipeReady();
      if (subprocess->done()) {
        _finished.push(*i);
        i = _running.erase(i);
        continue;
      }
    }
  }

  return isInterrupted();
}

#else  // !defined(USE_PPOLL)
bool SubprocessSet::doWork() {
  fd_set set;
  int nfds = 0;
  FD_ZERO(&set);

  for (const auto *subprocess : _running) {
    int fd = subprocess->_fd;
    if (fd >= 0) {
      FD_SET(fd, &set);
      if (nfds < fd + 1) {
        nfds = fd + 1;
      }
    }
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
        _finished.push(*i);
        i = _running.erase(i);
        continue;
      }
    }
    ++i;
  }

  return isInterrupted();
}
#endif  // !defined(USE_PPOLL)

Subprocess *SubprocessSet::nextFinished() {
  if (_finished.empty()) {
    return NULL;
  }
  Subprocess * const subproc = _finished.front();
  _finished.pop();
  return subproc;
}

void SubprocessSet::clear() {
  for (const auto *subprocess : _running) {
    // Since the foreground process is in our process group, it will receive
    // the interruption signal (i.e. SIGINT or SIGTERM) at the same time as us.
    if (!subprocess->_use_console) {
      kill(-subprocess->_pid, _interrupted);
    }
  }
  for (const auto *subprocess : _running) {
    delete subprocess;
  }
  _running.clear();
}

}  // namespace shk
