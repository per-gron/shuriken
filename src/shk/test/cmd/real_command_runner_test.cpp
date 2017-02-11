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

#include <catch.hpp>

#include "cmd/real_command_runner.h"

#include <string>

#ifndef _WIN32
// SetWithLots need setrlimit.
#include <stdio.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace shk {
namespace {
#ifdef _WIN32
const char* kSimpleCommand = "cmd /c dir \\";
#else
const char* kSimpleCommand = "ls /";
#endif

CommandRunner::Result runCommand(
    const std::string &command,
    const std::string &pool_name = "a_pool") {
  CommandRunner::Result result;
  const auto runner = makeRealCommandRunner();

  bool did_finish = false;
  runner->invoke(
      command,
      pool_name,
      [&](CommandRunner::Result &&result_) {
        result = std::move(result_);
        did_finish = true;
      });

  while (!runner->empty()) {
    // Pretend we discovered that stderr was ready for writing.
    runner->runCommands();
  }

  CHECK(did_finish);

  return result;
}

}  // anonymous namespace

void verifyInterrupted(const std::string &command) {
  const auto runner = makeRealCommandRunner();
  runner->invoke(
      command,
      "",
      [](CommandRunner::Result &&result) {
      });

  while (!runner->empty()) {
    const bool interrupted = runner->runCommands();
    if (interrupted) {
      return;
    }
  }

  CHECK(!"We should have been interrupted");
}

TEST_CASE("SubprocessSet") {
  // Run a command that fails and emits to stderr.
  SECTION("BadCommandStderr") {
    const auto result = runCommand("cmd /c ninja_no_such_command");
    CHECK(result.exit_status == ExitStatus::FAILURE);
    CHECK(result.output != "");
  }

  // Run a command that does not exist
  SECTION("NoSuchCommand") {
    const auto result = runCommand("ninja_no_such_command");
    CHECK(result.exit_status == ExitStatus::FAILURE);
    CHECK(result.output != "");
#ifdef _WIN32
    CHECK("CreateProcess failed: The system cannot find the file "
          "specified.\n" == result.output);
#endif
  }

  SECTION("InvokeFromCallback") {
    const auto runner = makeRealCommandRunner();

    // Push a lot of commands within the callback to increase the likelihood
    // of a crash in case the command runner uses a vector or something else
    // equally bad.
    const size_t num_cmds = 50;
    size_t done = 0;
    runner->invoke(
        "/bin/echo",
        "a_pool",
        [&](CommandRunner::Result &&result) {
          for (size_t i = 0; i < num_cmds; i++) {
            runner->invoke(
                "/bin/echo",
                "a_pool",
                [&](CommandRunner::Result &&result) {
                  done++;
                });
          }
        });

    while (!runner->empty()) {
      runner->runCommands();
    }

    CHECK(num_cmds == done);
  }

  SECTION("SizeFromCallback") {
    const auto runner = makeRealCommandRunner();

    bool invoked = false;
    runner->invoke("/bin/echo", "a_pool", [&](CommandRunner::Result &&result) {
      CHECK(runner->empty());
      invoked = true;
    });
    while (!runner->empty()) {
      runner->runCommands();
    }

    CHECK(invoked);
  }

  SECTION("DontRunCallbackFromDestructor") {
    bool called = false;

    {
      const auto runner = makeRealCommandRunner();
      runner->invoke(
          "/bin/echo",
          "a_pool",
          [&](CommandRunner::Result &&result) {
            called = true;
          });
    }

    CHECK(!called);
  }

#ifndef _WIN32

  SECTION("InterruptChild") {
    const auto result = runCommand("kill -INT $$");
    CHECK(result.exit_status == ExitStatus::INTERRUPTED);
  }

  SECTION("InterruptParent") {
    verifyInterrupted("kill -INT $PPID ; sleep 1");
  }

  SECTION("InterruptChildWithSigTerm") {
    const auto result = runCommand("kill -TERM $$");
    CHECK(result.exit_status == ExitStatus::INTERRUPTED);
  }

  SECTION("InterruptParentWithSigTerm") {
    verifyInterrupted("kill -TERM $PPID ; sleep 1");
  }

  SECTION("InterruptChildWithSigHup") {
    const auto result = runCommand("kill -HUP $$");
    CHECK(result.exit_status == ExitStatus::INTERRUPTED);
  }

  SECTION("InterruptParentWithSigHup") {
    verifyInterrupted("kill -HUP $PPID ; sleep 1");
  }

  // A shell command to check if the current process is connected to a terminal.
  // This is different from having stdin/stdout/stderr be a terminal. (For
  // instance consider the command "yes < /dev/null > /dev/null 2>&1".
  // As "ps" will confirm, "yes" could still be connected to a terminal, despite
  // not having any of the standard file descriptors be a terminal.
  const std::string kIsConnectedToTerminal = "tty < /dev/tty > /dev/null";

  SECTION("Console") {
    // Skip test if we don't have the console ourselves.
    if (isatty(0) && isatty(1) && isatty(2)) {
      // Test that stdin, stdout and stderr are a terminal.
      // Also check that the current process is connected to a terminal.
      const auto result = runCommand(
          "test -t 0 -a -t 1 -a -t 2 && " + kIsConnectedToTerminal,
          "console");
      CHECK(result.exit_status == ExitStatus::SUCCESS);
    }
  }

  SECTION("NoConsole") {
    const auto result = runCommand(kIsConnectedToTerminal);
    CHECK(result.exit_status != ExitStatus::SUCCESS);
  }

#endif

  SECTION("SetWithSingle") {
    const auto result = runCommand(kSimpleCommand);
    CHECK(result.exit_status == ExitStatus::SUCCESS);
    CHECK(result.output != "");
  }

  SECTION("SetWithMulti") {
    const auto runner = makeRealCommandRunner();

    const char* kCommands[3] = {
      kSimpleCommand,
#ifdef _WIN32
      "cmd /c echo hi",
      "cmd /c time /t",
#else
      "whoami",
      "pwd",
#endif
    };

    int finished_processes = 0;
    bool processes_done[3];
    for (int i = 0; i < 3; ++i) {
      processes_done[i] = 0;
    }

    for (int i = 0; i < 3; ++i) {
      runner->invoke(
          kCommands[i],
          "",
          [i, &processes_done, &finished_processes](
              CommandRunner::Result &&result) {
            CHECK(result.exit_status == ExitStatus::SUCCESS);
            CHECK("" != result.output);
            processes_done[i] = true;
            finished_processes++;
          });
    }

    CHECK(3u == runner->size());
    for (int i = 0; i < 3; ++i) {
      CHECK(!processes_done[i]);
    }

    while (!processes_done[0] || !processes_done[1] || !processes_done[2]) {
      CHECK(runner->size() > 0u);
      runner->runCommands();
    }

    CHECK(0u == runner->size());
    CHECK(3 == finished_processes);
  }

// OS X's process limit is less than 1025 by default
// (|sysctl kern.maxprocperuid| is 709 on 10.7 and 10.8 and less prior to that).
#if !defined(__APPLE__) && !defined(_WIN32)
  SECTION("SetWithLots") {
    const auto runner = makeRealCommandRunner();

    // Arbitrary big number; needs to be over 1024 to confirm we're no longer
    // hostage to pselect.
    const unsigned kNumProcs = 1025;

    // Make sure [ulimit -n] isn't going to stop us from working.
    rlimit rlim;
    CHECK(0 == getrlimit(RLIMIT_NOFILE, &rlim));
    if (rlim.rlim_cur < kNumProcs) {
      printf("Raise [ulimit -n] well above %u (currently %lu) to make this test go\n", kNumProcs, rlim.rlim_cur);
      return;
    }

    int num_procs_finished = 0;
    for (size_t i = 0; i < kNumProcs; ++i) {
      runner->invoke(
          "/bin/echo",
          "pool",
          [&](CommandRunner::Result &&result) {
            CHECK(ExitStatus::SUCCESS == result.exit_status§);
            CHECK("" != result.output);
            num_procs_finished++;
          });
    }
    while (!runner->empty()) {
      runner->runCommands();
    }
    CHECK(num_procs_finished == kNumProcs);
  }
#endif  // !__APPLE__ && !_WIN32

  // TODO: this test could work on Windows, just not sure how to simply
  // read stdin.
#ifndef _WIN32
  // Verify that a command that attempts to read stdin correctly thinks
  // that stdin is closed.
  SECTION("ReadStdin") {
    const auto result = runCommand("cat -");
    CHECK(result.exit_status == ExitStatus::SUCCESS);
  }
#endif  // _WIN32
}
}  // namespace shk
