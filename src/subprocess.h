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

#pragma once

#include <string>
#include <vector>
#include <queue>

#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#endif

#include "exit_status.h"

namespace shk {

/**
 * Subprocess wraps a single async subprocess.  It is entirely
 * passive: it expects the caller to notify it when its fds are ready
 * for reading, as well as call Finish() to reap the child once done()
 * is true.
 */
class Subprocess {
 public:
  ~Subprocess();

  /**
   * Returns ExitSuccess on successful process exit, ExitInterrupted if
   * the process was interrupted, ExitFailure if it otherwise failed.
   */
  ExitStatus finish();

  bool done() const;

  const std::string &getOutput() const;

 private:
  Subprocess(bool use_console);
  void start(class SubprocessSet *set, const std::string &command);
  void onPipeReady();

  std::string _buf;

#ifdef _WIN32
  /**
   * Set up pipe_ as the parent-side pipe of the subprocess; return the
   * other end of the pipe, usable in the child process.
   */
  HANDLE setupPipe(HANDLE ioport);

  HANDLE child_;
  HANDLE pipe_;
  OVERLAPPED overlapped_;
  char _overlapped_buf[4 << 10];
  bool _is_reading;
#else
  int _fd = -1;
  pid_t _pid = -1;
#endif
  bool _use_console;

  friend class SubprocessSet;
};

/**
 * SubprocessSet runs a ppoll/pselect() loop around a set of Subprocesses.
 * doWork() waits for any state change in subprocesses; _finished
 * is a queue of subprocesses as they finish.
 */
class SubprocessSet {
 public:
  SubprocessSet();
  ~SubprocessSet();

  Subprocess *add(const std::string &command, bool use_console = false);
  bool doWork();
  Subprocess *nextFinished();
  void clear();

  const std::vector<Subprocess *> &running() {
    return _running;
  }
  const std::queue<Subprocess *> &finished() {
    return _finished;
  }

 private:
  std::vector<Subprocess *> _running;
  std::queue<Subprocess *> _finished;

#ifdef _WIN32
  static BOOL WINAPI notifyInterrupted(DWORD dwCtrlType);
  static HANDLE _ioport;
#else
  static void setInterruptedFlag(int signum);
  static void handlePendingInterruption();
  /**
   * Store the signal number that causes the interruption.
   * 0 if not interruption.
   */
  static int _interrupted;

  static bool isInterrupted() { return _interrupted != 0; }

  struct sigaction _old_int_act;
  struct sigaction _old_term_act;
  sigset_t _old_mask;
#endif

  friend class Subprocess;
};

}  // namespace shk
