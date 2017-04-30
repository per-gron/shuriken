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

#include "daemon.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdexcept>
#include <string>
#include <string.h>
#include <unistd.h>

// Code inspired by http://www.microhowto.info/howto/cause_a_process_to_become_a_daemon_in_c.html

void daemon(
    const DaemonConfig &config,
    const std::function<void ()> &run) throw(std::runtime_error) {
  // Fork, allowing the parent process to continue.
  pid_t pid = fork();
  if (pid == -1) {
    throw std::runtime_error(
        std::string("failed to fork while daemonising: ") + strerror(errno));
  } else if (pid != 0) {
    return;
  }

  // Start a new session for the daemon.
  if (setsid() == -1) {
    throw std::runtime_error(
        std::string("failed to become a session leader while daemonising: ") +
            strerror(errno));
  }

  // Fork again, allowing the parent process to terminate.
  signal(SIGHUP, SIG_IGN);
  pid = fork();
  if (pid == -1) {
    throw std::runtime_error(
        std::string("failed to fork (2nd time) while daemonising: ") +
            strerror(errno));
  } else if (pid != 0) {
    _exit(0);
  }

  // Close then reopen standard file descriptors.
  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);
  if (open(config.stdin.c_str(), O_RDONLY) == -1) {
    throw std::runtime_error(
        std::string("failed to reopen stdin while daemonising: ") +
            strerror(errno));
  }
  if (open(config.stdout.c_str(), O_WRONLY | O_CREAT, 0644) == -1) {
    throw std::runtime_error(
        std::string("failed to reopen stdout while daemonising: ") +
            strerror(errno));
  }
  if (open(config.stderr.c_str(), O_RDWR | O_CREAT, 0644) == -1) {
    throw std::runtime_error(
        std::string("failed to reopen stderr while daemonising: ") +
            strerror(errno));
  }

  run();

  // Don't allow the daemon to continue as the parent process.
  _exit(0);
}
