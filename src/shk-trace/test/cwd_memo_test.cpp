#include <catch.hpp>

#include <sys/syscall.h>
#include <thread>

#include "cwd_memo.h"

extern "C" int __pthread_chdir(const char *path);

namespace shk {
namespace {

std::string getCwd() {
  char buf[1024];
  REQUIRE(getcwd(buf, sizeof(buf)));
  return buf;
}

}  // anonymous namespace

TEST_CASE("CwdMemo") {
  CwdMemo memo(1, "/initial");

  SECTION("InitialCwd") {
    CHECK(memo.getCwd(1, 34) == "/initial");
  }

  SECTION("GetUnknownCwd") {
    CHECK(memo.getCwd(2, 3) == "");
  }

  SECTION("Fork") {
    SECTION("Basic") {
      memo.fork(1, 100, 2);
      CHECK(memo.getCwd(2, 3) == "/initial");
    }

    SECTION("ForkUnknownPid") {
      memo.fork(2, 100, 3);
    }

    SECTION("ChdirInFork") {
      memo.fork(1, 100, 2);
      memo.chdir(2, "/modified");
      CHECK(memo.getCwd(1, 3) == "/initial");
      CHECK(memo.getCwd(2, 3) == "/modified");
    }

    SECTION("ChdirInParent") {
      memo.fork(1, 100, 2);
      memo.chdir(1, "/modified");
      CHECK(memo.getCwd(1, 3) == "/modified");
      CHECK(memo.getCwd(2, 3) == "/initial");
    }

    SECTION("ForkFromThreadWithLocalOverride") {
      memo.threadChdir(300, "/thread");
      memo.newThread(300, 301);
      memo.fork(1, 300, 2);
      CHECK(memo.getCwd(2, 301) == "/thread");
    }
  }

  SECTION("Chdir") {
    SECTION("Basic") {
      memo.chdir(1, "/other");
      CHECK(memo.getCwd(1, 2) == "/other");
    }
    SECTION("Override") {
      memo.chdir(1, "/other");
      memo.chdir(1, "/new_other");
      CHECK(memo.getCwd(1, 2) == "/new_other");
    }

    SECTION("ChdirWithUnknownPid") {
      memo.chdir(2, "/other");
      CHECK(memo.getCwd(2, 3) == "/other");
      CHECK(memo.getCwd(1, 3) == "/initial");
    }
  }

  SECTION("Exit") {
    SECTION("Basic") {
      memo.exit(1);
      CHECK(memo.getCwd(1, 3) == "");
    }

    SECTION("ChdirAfterExit") {
      memo.exit(1);
      memo.chdir(1, "/hey");
      CHECK(memo.getCwd(1, 3) == "/hey");
    }

    SECTION("ExitUnknownPid") {
      memo.exit(2);
    }
  }

  SECTION("Thread") {
    SECTION("NewThreadUnknownId") {
      memo.newThread(101, 102);
      CHECK(memo.getCwd(1, 101) == "/initial");
      CHECK(memo.getCwd(1, 102) == "/initial");
    }

    SECTION("ThreadChdirUnknownId") {
      memo.threadChdir(101, "/thread");
      CHECK(memo.getCwd(1, 101) == "/thread");
    }

    SECTION("ThreadChdirOverride") {
      memo.threadChdir(101, "/thread");
      memo.threadChdir(101, "/new_thread");
      CHECK(memo.getCwd(1, 101) == "/new_thread");
    }

    SECTION("ThreadChdirOverridesGlobalChdir") {
      memo.threadChdir(101, "/thread");
      memo.chdir(1, "/new_global");
      CHECK(memo.getCwd(1, 101) == "/thread");
    }

    SECTION("ThreadChdirInheritance") {
      memo.threadChdir(101, "/thread");
      memo.newThread(101, 102);
      CHECK(memo.getCwd(1, 102) == "/initial");
    }

    SECTION("ThreadChdirInParentThread") {
      memo.threadChdir(101, "/thread");
      memo.newThread(101, 102);
      memo.threadChdir(101, "/new_thread");
      CHECK(memo.getCwd(1, 101) == "/new_thread");
      CHECK(memo.getCwd(1, 102) == "/initial");
    }

    SECTION("ThreadChdirInChildThread") {
      memo.threadChdir(101, "/thread");
      memo.newThread(101, 102);
      memo.threadChdir(102, "/new_thread");
      CHECK(memo.getCwd(1, 101) == "/thread");
      CHECK(memo.getCwd(1, 102) == "/new_thread");
    }

    SECTION("ThreadExit") {
      memo.threadChdir(101, "/thread");
      memo.threadExit(101);
      CHECK(memo.getCwd(1, 101) == "/initial");
    }

    SECTION("ThreadChdirAfterExit") {
      memo.threadChdir(101, "/thread");
      memo.threadExit(101);
      memo.threadChdir(101, "/new_thread");
      CHECK(memo.getCwd(1, 101) == "/new_thread");
    }
  }

  SECTION("PthreadChdirSemantics") {
    // This section is for verifying that __pthread_chdir actually behaves the
    // way CwdMemo expects it to behave. It's not a test for CwdMemo itself.
    //
    // The behavior that CwdMemo implements is:
    //
    // * There is a per-process cwd.
    // * Also, each thread may have a thread-local cwd, which has predecence
    //   over the per-process cwd, even if chdir is called from that thread.
    // * When spawning a new thread, the child thread copies the parent thread's
    //   cwd.
    // * When the thread-local cwd is set, it always overrides the per-
    auto initial_cwd = getCwd();

    SECTION("SetThreadLocalWd") {
      __pthread_chdir("/");
      CHECK(getCwd() == "/");
    }

    SECTION("ForkFromThreadWithLocalOverride") {
      __pthread_chdir("/");

      pid_t pid = fork();
      REQUIRE(pid != -1);
      if (pid != 0) {  // parent
        int status;
        REQUIRE(waitpid(pid, &status, 0) != -1);
        CHECK(status == 0);
      } else {  // child
        // Expect the fork to have the thread-local override of the parent.
        exit(getCwd() == "/" ? 0 : 1);
      }
    }

    SECTION("SetThreadLocalThenGlobalWd") {
      __pthread_chdir("/");
      REQUIRE(chdir(initial_cwd.c_str()) == 0);
      CHECK(getCwd() == "/");
    }

    SECTION("ThreadLocalCwdOverridesGlobal") {
      __pthread_chdir("/");
      REQUIRE(chdir(initial_cwd.c_str()) == 0);
      CHECK(getCwd() == "/");
    }

    SECTION("NewThreadDoesNotInheritLocalCwd") {
      __pthread_chdir("/");
      std::thread([&] {
        CHECK(getCwd() != "/");
      }).join();
    }

    SECTION("GlobalCwdInOtherThreadDoesNotOverrideThreadLocal") {
      __pthread_chdir("/");
      std::thread([&] {
        REQUIRE(chdir(initial_cwd.c_str()) == 0);
      }).join();
      CHECK(getCwd() == "/");
    }

    SECTION("OtherThreadLocalOverrideDoesNotOverride") {
      std::thread([&] {
        __pthread_chdir("/");
        CHECK(getCwd() == "/");
      }).join();
      CHECK(getCwd() == initial_cwd);
    }

    REQUIRE(chdir(initial_cwd.c_str()) == 0);
    REQUIRE(__pthread_chdir(initial_cwd.c_str()) == 0);
  }
}

}  // namespace shk
