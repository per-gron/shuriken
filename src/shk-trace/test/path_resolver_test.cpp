#include <catch.hpp>

#include <deque>

#include "path_resolver.h"

namespace shk {
namespace {

struct FileEvent {
  pid_t pid;
  uintptr_t thread_id;
  EventType type;
  int at_fd;
  std::string path;
};

class MockPathResolverDelegate : public PathResolver::Delegate {
 public:
  ~MockPathResolverDelegate() {
    CHECK(_file_events.empty());
  }

  virtual void fileEvent(
      pid_t pid,
      uintptr_t thread_id,
      EventType type,
      int at_fd,
      std::string &&path) override {
    _file_events.push_back(
        FileEvent{ pid, thread_id, type, at_fd, std::move(path) });
  }

  FileEvent popFileEvent() {
    return popFrontAndReturn(_file_events);
  }

 private:
  template <typename Container>
  typename Container::value_type popFrontAndReturn(Container &container) {
    REQUIRE(!container.empty());
    auto result = container.front();
    container.pop_front();
    return result;
  }

  std::deque<FileEvent> _file_events;
};

}  // anonymous namespace

TEST_CASE("PathResolver") {
  MockPathResolverDelegate delegate;
  static constexpr pid_t kInitialPid = 1;
  static const std::string kInitialPath = "/initial_path";
  PathResolver pr(delegate, kInitialPid, std::string(kInitialPath));

  SECTION("NewThread") {
    // Not much to test here
    pr.newThread(kInitialPid, 2, 3);
  }

  SECTION("TerminateThread") {
    static constexpr uintptr_t kThreadId = 3;

    SECTION("Basic") {
      pr.newThread(kInitialPid, 2, kThreadId);
      pr.terminateThread(3);
    }

    SECTION("ForgetThreadCwd") {
      pr.newThread(kInitialPid, 2, kThreadId);
      pr.threadChdir(kInitialPid, kThreadId, "/thread_path", AT_FDCWD);

      pr.fileEvent(
          kInitialPid,
          kThreadId,
          EventType::FATAL_ERROR,
          AT_FDCWD,
          "yoyo");
      CHECK(delegate.popFileEvent().path == "/thread_path/yoyo");

      pr.terminateThread(kThreadId);

      pr.fileEvent(
          kInitialPid,
          kThreadId,
          EventType::FATAL_ERROR,
          AT_FDCWD,
          "yoyo");
      CHECK(delegate.popFileEvent().path == kInitialPath + "/yoyo");
    }
  }

  SECTION("FileEvent") {
    SECTION("Absolute") {
      pr.fileEvent(kInitialPid, 2, EventType::FATAL_ERROR, 3, "/yoyo");
      auto event = delegate.popFileEvent();
      CHECK(event.pid == kInitialPid);
      CHECK(event.thread_id == 2);
      CHECK(event.type == EventType::FATAL_ERROR);
      CHECK(event.at_fd == AT_FDCWD);
      CHECK(event.path == "/yoyo");
    }

    SECTION("RelativeToCwd") {
      pr.fileEvent(kInitialPid, 2, EventType::FATAL_ERROR, AT_FDCWD, "yoyo");
      auto event = delegate.popFileEvent();
      CHECK(event.pid == kInitialPid);
      CHECK(event.thread_id == 2);
      CHECK(event.type == EventType::FATAL_ERROR);
      CHECK(event.at_fd == AT_FDCWD);
      CHECK(event.path == kInitialPath + "/yoyo");
    }

    SECTION("RelativeToFd") {
      static constexpr int kFd = 3;
      static const std::string kFdPath = "/fd";

      pr.open(kInitialPid, 2, kFd, AT_FDCWD, std::string(kFdPath), true);
      pr.fileEvent(kInitialPid, 2, EventType::FATAL_ERROR, kFd, "yoyo");
      auto event = delegate.popFileEvent();
      CHECK(event.pid == kInitialPid);
      CHECK(event.thread_id == 2);
      CHECK(event.type == EventType::FATAL_ERROR);
      CHECK(event.at_fd == AT_FDCWD);
      CHECK(event.path == kFdPath + "/yoyo");
    }
  }

  SECTION("Open") {
    static constexpr int kFd = 3;
    static constexpr int kFd2 = 4;
    static const std::string kFdPath1 = "/fd1";
    static const std::string kFdPath2 = "/fd2";
    static const std::string kRelFdPath = "relfd";

    SECTION("InDifferentPid") {
      pr.open(kInitialPid, 2, kFd, AT_FDCWD, std::string(kFdPath1), true);
      pr.open(kInitialPid + 1, 2, kFd, AT_FDCWD, std::string(kFdPath2), true);

      pr.fileEvent(kInitialPid, 2, EventType::FATAL_ERROR, kFd, "yoyo");
      CHECK(delegate.popFileEvent().path == kFdPath1 + "/yoyo");

      pr.fileEvent(kInitialPid + 1, 2, EventType::FATAL_ERROR, kFd, "yoyo");
      CHECK(delegate.popFileEvent().path == kFdPath2 + "/yoyo");
    }

    SECTION("InDifferentThread") {
      pr.open(kInitialPid, 2, kFd, AT_FDCWD, std::string(kFdPath1), true);
      // Should overwrite previous fd; fds are not per-thread
      pr.open(kInitialPid, 3, kFd, AT_FDCWD, std::string(kFdPath2), true);

      pr.fileEvent(kInitialPid, 2, EventType::FATAL_ERROR, kFd, "yoyo");
      CHECK(delegate.popFileEvent().path == kFdPath2 + "/yoyo");

      pr.fileEvent(kInitialPid, 3, EventType::FATAL_ERROR, kFd, "yoyo");
      CHECK(delegate.popFileEvent().path == kFdPath2 + "/yoyo");
    }

    SECTION("AbsolutePath") {
      pr.open(kInitialPid, 2, kFd, AT_FDCWD, std::string(kFdPath1), true);
      pr.fileEvent(kInitialPid, 2, EventType::FATAL_ERROR, kFd, "yoyo");
      CHECK(delegate.popFileEvent().path == kFdPath1 + "/yoyo");
    }

    SECTION("RelativeToCwd") {
      pr.open(kInitialPid, 2, kFd, AT_FDCWD, std::string(kRelFdPath), true);
      pr.fileEvent(kInitialPid, 2, EventType::FATAL_ERROR, kFd, "yoyo");
      CHECK(
          delegate.popFileEvent().path ==
          kInitialPath + "/" + kRelFdPath + "/yoyo");
    }

    SECTION("RelativeToFd") {
      pr.open(kInitialPid, 2, kFd, AT_FDCWD, std::string(kFdPath1), true);
      pr.open(kInitialPid, 2, kFd2, kFd, std::string(kRelFdPath), true);
      pr.fileEvent(kInitialPid, 2, EventType::FATAL_ERROR, kFd2, "yoyo");
      CHECK(
          delegate.popFileEvent().path ==
          kFdPath1 + "/" + kRelFdPath + "/yoyo");
    }

    SECTION("Cloexec") {
      SECTION("CloexecOff") {
        pr.open(kInitialPid, 2, kFd, AT_FDCWD, std::string(kFdPath1), false);
        pr.exec(kInitialPid, 2);
        pr.fileEvent(kInitialPid, 2, EventType::FATAL_ERROR, kFd, "yoyo");
        CHECK(delegate.popFileEvent().path == kFdPath1 + "/yoyo");
      }

      SECTION("CloexecOn") {
        pr.open(kInitialPid, 2, kFd, AT_FDCWD, std::string(kFdPath1), true);
        pr.exec(kInitialPid, 2);
        pr.fileEvent(kInitialPid, 2, EventType::FATAL_ERROR, kFd, "yoyo");
        // Should have lost the path info of kFd by now
        CHECK(delegate.popFileEvent().path == "/yoyo");
      }
    }
  }

  SECTION("Dup") {
    static constexpr int kFd1 = 3;
    static constexpr int kFd2 = 4;
    static const std::string kFdPath = "/fd";

    SECTION("UnknownFd") {
      pr.dup(kInitialPid, 2, 123, 124, false);
    }

    SECTION("KnownFd") {
      pr.open(kInitialPid, 2, kFd1, AT_FDCWD, std::string(kFdPath), true);
      pr.dup(kInitialPid, 2, kFd1, kFd2, false);
      pr.fileEvent(kInitialPid, 2, EventType::FATAL_ERROR, kFd2, "yoyo");
      CHECK(delegate.popFileEvent().path == kFdPath + "/yoyo");
    }

    SECTION("KnownFdDifferentThread") {
      pr.open(kInitialPid, 2, kFd1, AT_FDCWD, std::string(kFdPath), true);
      pr.dup(kInitialPid, 3, kFd1, kFd2, false);
      pr.fileEvent(kInitialPid, 2, EventType::FATAL_ERROR, kFd2, "yoyo");
      CHECK(delegate.popFileEvent().path == kFdPath + "/yoyo");
    }

    SECTION("KnownFdDifferentPid") {
      pr.open(kInitialPid, 2, kFd1, AT_FDCWD, std::string(kFdPath), true);
      pr.dup(kInitialPid, 3, kFd1, kFd2, false);
      pr.fileEvent(kInitialPid + 1, 2, EventType::FATAL_ERROR, kFd2, "yoyo");
      // Should not recognize the fd
      CHECK(delegate.popFileEvent().path == "/yoyo");
    }

    SECTION("OnFork") {
      pr.open(kInitialPid, 2, kFd1, AT_FDCWD, std::string(kFdPath), false);
      pr.dup(kInitialPid, 3, kFd1, kFd2, false);
      pr.fork(kInitialPid, 10, kInitialPid + 1);

      SECTION("NotClosed") {
        pr.fileEvent(kInitialPid, 2, EventType::FATAL_ERROR, kFd2, "yoyo");
        CHECK(delegate.popFileEvent().path == kFdPath + "/yoyo");

        pr.fileEvent(kInitialPid + 1, 2, EventType::FATAL_ERROR, kFd2, "yoyo");
        CHECK(delegate.popFileEvent().path == kFdPath + "/yoyo");
      }


      SECTION("CloseInParentProcess") {
        pr.close(kInitialPid, 2, kFd2);

        pr.fileEvent(kInitialPid, 2, EventType::FATAL_ERROR, kFd2, "yoyo");
        // Should not recognize the fd
        CHECK(delegate.popFileEvent().path == "/yoyo");

        pr.fileEvent(kInitialPid + 1, 2, EventType::FATAL_ERROR, kFd2, "yoyo");
        CHECK(delegate.popFileEvent().path == kFdPath + "/yoyo");
      }

      SECTION("CloseInChildProcess") {
        pr.close(kInitialPid + 1, 2, kFd2);

        pr.fileEvent(kInitialPid, 2, EventType::FATAL_ERROR, kFd2, "yoyo");
        CHECK(delegate.popFileEvent().path == kFdPath + "/yoyo");

        pr.fileEvent(kInitialPid + 1, 2, EventType::FATAL_ERROR, kFd2, "yoyo");
        // Should not recognize the fd
        CHECK(delegate.popFileEvent().path == "/yoyo");
      }
    }

    SECTION("Cloexec") {
      SECTION("CloexecSetOff") {
        pr.open(kInitialPid, 2, kFd1, AT_FDCWD, std::string(kFdPath), true);
        pr.dup(kInitialPid, 2, kFd1, kFd2, false);
        pr.exec(kInitialPid, 2);

        pr.fileEvent(kInitialPid, 2, EventType::FATAL_ERROR, kFd1, "yoyo");
        // Should not recognize the fd
        CHECK(delegate.popFileEvent().path == "/yoyo");

        pr.fileEvent(kInitialPid, 2, EventType::FATAL_ERROR, kFd2, "yoyo");
        CHECK(delegate.popFileEvent().path == kFdPath + "/yoyo");
      }

      SECTION("CloexecSetOn") {
        pr.open(kInitialPid, 2, kFd1, AT_FDCWD, std::string(kFdPath), false);
        pr.dup(kInitialPid, 2, kFd1, kFd2, true);
        pr.exec(kInitialPid, 2);

        pr.fileEvent(kInitialPid, 2, EventType::FATAL_ERROR, kFd1, "yoyo");
        CHECK(delegate.popFileEvent().path == kFdPath + "/yoyo");

        pr.fileEvent(kInitialPid, 2, EventType::FATAL_ERROR, kFd2, "yoyo");
        // Should not recognize the fd
        CHECK(delegate.popFileEvent().path == "/yoyo");
      }
    }
  }

  SECTION("SetCloexec") {
    static constexpr int kFd = 3;
    static const std::string kFdPath = "/fd";

    SECTION("CloexecSetOff") {
      pr.open(kInitialPid, 2, kFd, AT_FDCWD, std::string(kFdPath), true);
      pr.setCloexec(kInitialPid, 2, kFd, false);
      pr.exec(kInitialPid, 2);

      pr.fileEvent(kInitialPid, 2, EventType::FATAL_ERROR, kFd, "yoyo");
      CHECK(delegate.popFileEvent().path == kFdPath + "/yoyo");
    }

    SECTION("CloexecSetOn") {
      pr.open(kInitialPid, 2, kFd, AT_FDCWD, std::string(kFdPath), false);
      pr.setCloexec(kInitialPid, 2, kFd, true);
      pr.exec(kInitialPid, 2);

      pr.fileEvent(kInitialPid, 2, EventType::FATAL_ERROR, kFd, "yoyo");
      // Should not recognize the fd
      CHECK(delegate.popFileEvent().path == "/yoyo");
    }
  }

  SECTION("Fork") {
    // Tested in the Dup/OnFork section
  }

  SECTION("Close") {
    static constexpr int kFd = 3;
    static const std::string kFdPath = "/fd";
    pr.open(kInitialPid, 2, kFd, AT_FDCWD, std::string(kFdPath), false);
    pr.close(kInitialPid, 2, kFd);

    pr.fileEvent(kInitialPid, 2, EventType::FATAL_ERROR, kFd, "yoyo");
    // Should not recognize the fd
    CHECK(delegate.popFileEvent().path == "/yoyo");
  }

  SECTION("Chdir") {
    static constexpr int kFd = 3;
    static const std::string kNewPath = "/new";
    static const std::string kFdPath = "/fd";
    pr.chdir(kInitialPid + 1, 3, std::string(kInitialPath), AT_FDCWD);

    SECTION("RelativeToFd") {
      pr.open(kInitialPid, 2, kFd, AT_FDCWD, std::string(kFdPath), false);
      pr.chdir(kInitialPid, 2, "a_path", kFd);

      pr.fileEvent(kInitialPid, 2, EventType::FATAL_ERROR, AT_FDCWD, "yoyo");
      CHECK(delegate.popFileEvent().path == kFdPath + "/a_path/yoyo");
    }

    SECTION("RelativeToCwd") {
      pr.chdir(kInitialPid, 2, "a_path", AT_FDCWD);

      pr.fileEvent(kInitialPid, 2, EventType::FATAL_ERROR, AT_FDCWD, "yoyo");
      CHECK(delegate.popFileEvent().path == kInitialPath + "/a_path/yoyo");
    }

    SECTION("UnknownPid") {
      pr.fileEvent(kInitialPid + 1, 3, EventType::FATAL_ERROR, AT_FDCWD, "yoyo");
      CHECK(delegate.popFileEvent().path == kInitialPath + "/yoyo");
    }

    SECTION("AcrossThreads") {
      pr.chdir(kInitialPid, 3, std::string(kNewPath), AT_FDCWD);

      pr.fileEvent(kInitialPid, 2, EventType::FATAL_ERROR, AT_FDCWD, "yoyo");
      CHECK(delegate.popFileEvent().path == kNewPath + "/yoyo");
    }

    SECTION("AcrossProcesses") {
      pr.chdir(kInitialPid, 3, std::string(kNewPath), AT_FDCWD);

      pr.fileEvent(kInitialPid + 1, 2, EventType::FATAL_ERROR, AT_FDCWD, "yoyo");
      CHECK(delegate.popFileEvent().path == kInitialPath + "/yoyo");
    }

    SECTION("OnFork") {
      pr.fork(kInitialPid, 2, kInitialPid + 2);

      SECTION("ChdirInParent") {
        pr.chdir(kInitialPid, 2, std::string(kNewPath), AT_FDCWD);

        pr.fileEvent(kInitialPid, 2, EventType::FATAL_ERROR, AT_FDCWD, "yoyo");
        CHECK(delegate.popFileEvent().path == kNewPath + "/yoyo");

        pr.fileEvent(kInitialPid + 2, 2, EventType::FATAL_ERROR, AT_FDCWD, "yoyo");
        CHECK(delegate.popFileEvent().path == kInitialPath + "/yoyo");
      }

      SECTION("ChdirInChild") {
        pr.chdir(kInitialPid + 2, 2, std::string(kNewPath), AT_FDCWD);

        pr.fileEvent(kInitialPid, 2, EventType::FATAL_ERROR, AT_FDCWD, "yoyo");
        CHECK(delegate.popFileEvent().path == kInitialPath + "/yoyo");

        pr.fileEvent(kInitialPid + 2, 2, EventType::FATAL_ERROR, AT_FDCWD, "yoyo");
        CHECK(delegate.popFileEvent().path == kNewPath + "/yoyo");
      }
    }
  }

  SECTION("ThreadChdir") {
    static constexpr int kFd = 3;
    static const std::string kNewPath = "/new";
    static const std::string kNewerPath = "/newer";
    static const std::string kFdPath = "/fd";
    static const uintptr_t kThreadId1 = 101;
    static const uintptr_t kThreadId2 = 102;
    pr.threadChdir(kInitialPid, kThreadId1, std::string(kNewPath), AT_FDCWD);

    SECTION("SameThread") {
      pr.fileEvent(kInitialPid, kThreadId1, EventType::FATAL_ERROR, AT_FDCWD, "yoyo");
      CHECK(delegate.popFileEvent().path == kNewPath + "/yoyo");
    }

    SECTION("AcrossThreads") {
      pr.fileEvent(kInitialPid, kThreadId2, EventType::FATAL_ERROR, AT_FDCWD, "yoyo");
      CHECK(delegate.popFileEvent().path == kInitialPath + "/yoyo");
    }

    SECTION("RelativeToFd") {
      pr.open(kInitialPid, kThreadId1, kFd, AT_FDCWD, std::string(kFdPath), false);
      pr.threadChdir(kInitialPid, kThreadId1, "a_path", kFd);

      pr.fileEvent(kInitialPid, kThreadId1, EventType::FATAL_ERROR, AT_FDCWD, "yoyo");
      CHECK(delegate.popFileEvent().path == kFdPath + "/a_path/yoyo");
    }

    SECTION("RelativeToCwd") {
      pr.threadChdir(kInitialPid, kThreadId1, "a_path", AT_FDCWD);

      pr.fileEvent(kInitialPid, kThreadId1, EventType::FATAL_ERROR, AT_FDCWD, "yoyo");
      CHECK(delegate.popFileEvent().path == kNewPath + "/a_path/yoyo");
    }

    SECTION("NewThread") {
      pr.newThread(kInitialPid, kThreadId1, kThreadId2);

      SECTION("InheritThreadLocalCwd") {
        pr.fileEvent(kInitialPid, kThreadId2, EventType::FATAL_ERROR, AT_FDCWD, "yoyo");
        CHECK(delegate.popFileEvent().path == kNewPath + "/yoyo");
      }

      SECTION("ChdirInParent") {
        pr.threadChdir(kInitialPid, kThreadId1, std::string(kNewerPath), AT_FDCWD);

        pr.fileEvent(kInitialPid, kThreadId1, EventType::FATAL_ERROR, AT_FDCWD, "yoyo");
        CHECK(delegate.popFileEvent().path == kNewerPath + "/yoyo");

        pr.fileEvent(kInitialPid, kThreadId2, EventType::FATAL_ERROR, AT_FDCWD, "yoyo");
        CHECK(delegate.popFileEvent().path == kNewPath + "/yoyo");
      }

      SECTION("ChdirInChild") {
        pr.threadChdir(kInitialPid, kThreadId2, std::string(kNewerPath), AT_FDCWD);

        pr.fileEvent(kInitialPid, kThreadId1, EventType::FATAL_ERROR, AT_FDCWD, "yoyo");
        CHECK(delegate.popFileEvent().path == kNewPath + "/yoyo");

        pr.fileEvent(kInitialPid, kThreadId2, EventType::FATAL_ERROR, AT_FDCWD, "yoyo");
        CHECK(delegate.popFileEvent().path == kNewerPath + "/yoyo");
      }
    }
  }

  SECTION("Exec") {
    // Tested in the Open/Cloexec section
  }
}

}  // namespace shk
