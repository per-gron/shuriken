#include <catch.hpp>

#include <deque>

#include "path_resolver.h"

namespace shk {
namespace {

struct FileEvent {
  EventType type;
  std::string path;
};

class MockPathResolverDelegate : public PathResolver::Delegate {
 public:
  ~MockPathResolverDelegate() {
    CHECK(_file_events.empty());
  }

  virtual void fileEvent(
      EventType type,
      std::string &&path) override {
    _file_events.push_back(
        FileEvent{ type, std::move(path) });
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
  auto delegate_ptr = std::unique_ptr<MockPathResolverDelegate>(
      new MockPathResolverDelegate());
  MockPathResolverDelegate &delegate = *delegate_ptr;
  static constexpr pid_t kInitialPid = 1;
  static constexpr uintptr_t kThreadId = 3;
  static const uintptr_t kThreadId2 = 102;
  static const std::string kInitialPath = "/initial_path";
  PathResolver pr(std::move(delegate_ptr), kInitialPid, std::string(kInitialPath));
  pr.newThread(kInitialPid, 2, kThreadId);

  SECTION("NewThread") {
    // Not much to test here.. It's tested above in the test set-up.

    SECTION("Fork") {
      // Tested in the Dup/OnFork section
    }
  }

  SECTION("TerminateThread") {
    SECTION("Basic") {
      CHECK(pr.terminateThread(kThreadId) == Tracer::Delegate::Response::OK);
    }

    SECTION("ForgetThreadCwd") {
      pr.threadChdir(kThreadId, "/thread_path", AT_FDCWD);

      pr.fileEvent(
          kThreadId,
          EventType::READ,
          AT_FDCWD,
          "yoyo");
      CHECK(delegate.popFileEvent().path == "/thread_path/yoyo");

      pr.terminateThread(kThreadId);

      pr.fileEvent(
          kThreadId,
          EventType::READ,
          AT_FDCWD,
          "yoyo");
      CHECK(delegate.popFileEvent().path == "yoyo");
    }
  }

  SECTION("FileEvent") {
    SECTION("FatalError") {
      // Paths should not be resolved in thi scase
      pr.fileEvent(kThreadId, EventType::FATAL_ERROR, 3, "yoyo");
      auto event = delegate.popFileEvent();
      CHECK(event.type == EventType::FATAL_ERROR);
      CHECK(event.path == "yoyo");
    }

    SECTION("Absolute") {
      pr.fileEvent(kThreadId, EventType::READ, 3, "/yoyo");
      auto event = delegate.popFileEvent();
      CHECK(event.type == EventType::READ);
      CHECK(event.path == "/yoyo");
    }

    SECTION("RelativeToCwd") {
      pr.fileEvent(kThreadId, EventType::READ, AT_FDCWD, "yoyo");
      auto event = delegate.popFileEvent();
      CHECK(event.type == EventType::READ);
      CHECK(event.path == kInitialPath + "/yoyo");
    }

    SECTION("EmptyCwd") {
      auto delegate_ptr = std::unique_ptr<MockPathResolverDelegate>(
          new MockPathResolverDelegate());
      MockPathResolverDelegate &delegate = *delegate_ptr;
      PathResolver pr(std::move(delegate_ptr), kInitialPid, "");
      pr.newThread(kInitialPid, 2, kThreadId);
      pr.fileEvent(kThreadId, EventType::READ, AT_FDCWD, "yoyo");
      auto event = delegate.popFileEvent();
      CHECK(event.type == EventType::READ);
      CHECK(event.path == "/yoyo");
    }

    SECTION("CwdEndingWithSlash") {
      pr.chdir(kThreadId, "/", AT_FDCWD);
      pr.fileEvent(kThreadId, EventType::READ, AT_FDCWD, "yoyo");
      auto event = delegate.popFileEvent();
      CHECK(event.type == EventType::READ);
      CHECK(event.path == "/yoyo");
    }

    SECTION("EmptyPath") {
      pr.fileEvent(kThreadId, EventType::READ, AT_FDCWD, "");
      auto event = delegate.popFileEvent();
      CHECK(event.path == kInitialPath);
    }

    SECTION("RelativeToFd") {
      static constexpr int kFd = 3;
      static const std::string kFdPath = "/fd";

      pr.open(kThreadId, kFd, AT_FDCWD, std::string(kFdPath), true);
      pr.fileEvent(kThreadId, EventType::READ, kFd, "yoyo");
      auto event = delegate.popFileEvent();
      CHECK(event.type == EventType::READ);
      CHECK(event.path == kFdPath + "/yoyo");
    }
  }

  SECTION("Open") {
    static constexpr int kFd = 3;
    static constexpr int kFd2 = 4;
    static const std::string kFdPath1 = "/fd1";
    static const std::string kFdPath2 = "/fd2";
    static const std::string kRelFdPath = "relfd";

    SECTION("InDifferentProcesses") {
      pr.newThread(kInitialPid + 1, kThreadId, kThreadId2);

      pr.open(kThreadId, kFd, AT_FDCWD, std::string(kFdPath1), true);
      pr.open(kThreadId2, kFd, AT_FDCWD, std::string(kFdPath2), true);

      pr.fileEvent(kThreadId, EventType::READ, kFd, "yoyo");
      CHECK(delegate.popFileEvent().path == kFdPath1 + "/yoyo");

      pr.fileEvent(kThreadId2, EventType::READ, kFd, "yoyo");
      CHECK(delegate.popFileEvent().path == kFdPath2 + "/yoyo");
    }

    SECTION("InUnknownThread") {
      // Should have no effect
      pr.open(9876, kFd, AT_FDCWD, std::string(kFdPath1), true);
    }

    SECTION("InDifferentThreadsInSameProcess") {
      pr.newThread(kInitialPid, kThreadId, kThreadId2);

      pr.open(kThreadId, kFd, AT_FDCWD, std::string(kFdPath1), true);
      // Should overwrite previous fd; fds are not per-thread
      pr.open(kThreadId2, kFd, AT_FDCWD, std::string(kFdPath2), true);

      pr.fileEvent(kThreadId, EventType::READ, kFd, "yoyo");
      CHECK(delegate.popFileEvent().path == kFdPath2 + "/yoyo");

      pr.fileEvent(kThreadId2, EventType::READ, kFd, "yoyo");
      CHECK(delegate.popFileEvent().path == kFdPath2 + "/yoyo");
    }

    SECTION("AbsolutePath") {
      pr.open(kThreadId, kFd, AT_FDCWD, std::string(kFdPath1), true);
      pr.fileEvent(kThreadId, EventType::READ, kFd, "yoyo");
      CHECK(delegate.popFileEvent().path == kFdPath1 + "/yoyo");
    }

    SECTION("RelativeToCwd") {
      pr.open(kThreadId, kFd, AT_FDCWD, std::string(kRelFdPath), true);
      pr.fileEvent(kThreadId, EventType::READ, kFd, "yoyo");
      CHECK(
          delegate.popFileEvent().path ==
          kInitialPath + "/" + kRelFdPath + "/yoyo");
    }

    SECTION("RelativeToFd") {
      pr.open(kThreadId, kFd, AT_FDCWD, std::string(kFdPath1), true);
      pr.open(kThreadId, kFd2, kFd, std::string(kRelFdPath), true);
      pr.fileEvent(kThreadId, EventType::READ, kFd2, "yoyo");
      CHECK(
          delegate.popFileEvent().path ==
          kFdPath1 + "/" + kRelFdPath + "/yoyo");
    }

    SECTION("Cloexec") {
      SECTION("CloexecOff") {
        pr.open(kThreadId, kFd, AT_FDCWD, std::string(kFdPath1), false);
        pr.exec(kThreadId);
        pr.fileEvent(kThreadId, EventType::READ, kFd, "yoyo");
        CHECK(delegate.popFileEvent().path == kFdPath1 + "/yoyo");
      }

      SECTION("CloexecOn") {
        pr.open(kThreadId, kFd, AT_FDCWD, std::string(kFdPath1), true);
        pr.exec(kThreadId);
        pr.fileEvent(kThreadId, EventType::READ, kFd, "yoyo");
        // Should have lost the path info of kFd by now
        CHECK(delegate.popFileEvent().path == "/yoyo");
      }
    }
  }

  SECTION("Dup") {
    static constexpr int kFd1 = 3;
    static constexpr int kFd2 = 4;
    static const std::string kFdPath = "/fd";

    SECTION("UnknownThread") {
      pr.dup(34243, kFd1, kFd2, false);
    }

    SECTION("UnknownFd") {
      pr.dup(kThreadId, 123, 124, false);
    }

    SECTION("KnownFd") {
      pr.open(kThreadId, kFd1, AT_FDCWD, std::string(kFdPath), true);
      pr.dup(kThreadId, kFd1, kFd2, false);
      pr.fileEvent(kThreadId, EventType::READ, kFd2, "yoyo");
      CHECK(delegate.popFileEvent().path == kFdPath + "/yoyo");
    }

    SECTION("KnownFdDifferentThreadSameProcess") {
      pr.newThread(kInitialPid, kThreadId, kThreadId2);

      pr.open(kThreadId, kFd1, AT_FDCWD, std::string(kFdPath), true);
      pr.dup(kThreadId2, kFd1, kFd2, false);
      pr.fileEvent(kThreadId, EventType::READ, kFd2, "yoyo");
      CHECK(delegate.popFileEvent().path == kFdPath + "/yoyo");
    }

    SECTION("KnownFdDifferentPid") {
      pr.newThread(kInitialPid + 1, 1, kThreadId2);

      pr.open(kThreadId, kFd1, AT_FDCWD, std::string(kFdPath), true);
      pr.dup(kThreadId, kFd1, kFd2, false);
      pr.fileEvent(kThreadId2, EventType::READ, kFd2, "yoyo");
      // Should not recognize the fd
      CHECK(delegate.popFileEvent().path == "/yoyo");
    }

    SECTION("KnownFdUnknownThreadId") {
      pr.open(kThreadId, kFd1, AT_FDCWD, std::string(kFdPath), true);
      pr.dup(kThreadId, kFd1, kFd2, false);
      pr.fileEvent(kThreadId2, EventType::READ, kFd2, "yoyo");
      // Should not recognize the fd
      CHECK(delegate.popFileEvent().path == "yoyo");
    }

    SECTION("OnFork") {
      pr.open(kThreadId, kFd1, AT_FDCWD, std::string(kFdPath), false);
      pr.dup(kThreadId, kFd1, kFd2, false);
      pr.newThread(kInitialPid + 1, kThreadId, kThreadId2);

      SECTION("NotClosed") {
        pr.fileEvent(kThreadId, EventType::READ, kFd2, "yoyo");
        CHECK(delegate.popFileEvent().path == kFdPath + "/yoyo");

        pr.fileEvent(kThreadId2, EventType::READ, kFd2, "yoyo");
        CHECK(delegate.popFileEvent().path == kFdPath + "/yoyo");
      }


      SECTION("CloseInParentProcess") {
        pr.close(kThreadId, kFd2);

        pr.fileEvent(kThreadId, EventType::READ, kFd2, "yoyo");
        // Should not recognize the fd
        CHECK(delegate.popFileEvent().path == "/yoyo");

        pr.fileEvent(kThreadId2, EventType::READ, kFd2, "yoyo");
        CHECK(delegate.popFileEvent().path == kFdPath + "/yoyo");
      }

      SECTION("CloseInChildProcess") {
        pr.close(kThreadId2, kFd2);

        pr.fileEvent(kThreadId, EventType::READ, kFd2, "yoyo");
        CHECK(delegate.popFileEvent().path == kFdPath + "/yoyo");

        pr.fileEvent(kThreadId2, EventType::READ, kFd2, "yoyo");
        // Should not recognize the fd
        CHECK(delegate.popFileEvent().path == "/yoyo");
      }
    }

    SECTION("Cloexec") {
      SECTION("CloexecSetOff") {
        pr.open(kThreadId, kFd1, AT_FDCWD, std::string(kFdPath), true);
        pr.dup(kThreadId, kFd1, kFd2, false);
        pr.exec(kThreadId);

        pr.fileEvent(kThreadId, EventType::READ, kFd1, "yoyo");
        // Should not recognize the fd
        CHECK(delegate.popFileEvent().path == "/yoyo");

        pr.fileEvent(kThreadId, EventType::READ, kFd2, "yoyo");
        CHECK(delegate.popFileEvent().path == kFdPath + "/yoyo");
      }

      SECTION("CloexecSetOn") {
        pr.open(kThreadId, kFd1, AT_FDCWD, std::string(kFdPath), false);
        pr.dup(kThreadId, kFd1, kFd2, true);
        pr.exec(kThreadId);

        pr.fileEvent(kThreadId, EventType::READ, kFd1, "yoyo");
        CHECK(delegate.popFileEvent().path == kFdPath + "/yoyo");

        pr.fileEvent(kThreadId, EventType::READ, kFd2, "yoyo");
        // Should not recognize the fd
        CHECK(delegate.popFileEvent().path == "/yoyo");
      }
    }
  }

  SECTION("SetCloexec") {
    static constexpr int kFd = 3;
    static const std::string kFdPath = "/fd";

    SECTION("UnknownThread") {
      pr.setCloexec(543523, kFd, false);
    }

    SECTION("CloexecSetOff") {
      pr.open(kThreadId, kFd, AT_FDCWD, std::string(kFdPath), true);
      pr.setCloexec(kThreadId, kFd, false);
      pr.exec(kThreadId);

      pr.fileEvent(kThreadId, EventType::READ, kFd, "yoyo");
      CHECK(delegate.popFileEvent().path == kFdPath + "/yoyo");
    }

    SECTION("CloexecSetOn") {
      pr.open(kThreadId, kFd, AT_FDCWD, std::string(kFdPath), false);
      pr.setCloexec(kThreadId, kFd, true);
      pr.exec(kThreadId);

      pr.fileEvent(kThreadId, EventType::READ, kFd, "yoyo");
      // Should not recognize the fd
      CHECK(delegate.popFileEvent().path == "/yoyo");
    }
  }

  SECTION("Close") {
    static constexpr int kFd = 3;
    static const std::string kFdPath = "/fd";
    pr.open(kThreadId, kFd, AT_FDCWD, std::string(kFdPath), false);
    pr.close(kThreadId, kFd);

    pr.fileEvent(kThreadId, EventType::READ, kFd, "yoyo");
    // Should not recognize the fd
    CHECK(delegate.popFileEvent().path == "/yoyo");
  }

  SECTION("Chdir") {
    static constexpr int kFd = 3;
    static const uintptr_t kThreadId3 = 103;
    static const std::string kNewPath = "/new";
    static const std::string kFdPath = "/fd";
    pr.newThread(kInitialPid + 1, 0, kThreadId2);
    pr.chdir(kThreadId2, std::string(kInitialPath), AT_FDCWD);

    SECTION("RelativeToFd") {
      pr.open(kThreadId, kFd, AT_FDCWD, std::string(kFdPath), false);
      pr.chdir(kThreadId, "a_path", kFd);

      pr.fileEvent(kThreadId, EventType::READ, AT_FDCWD, "yoyo");
      CHECK(delegate.popFileEvent().path == kFdPath + "/a_path/yoyo");
    }

    SECTION("RelativeToCwd") {
      pr.chdir(kThreadId, "a_path", AT_FDCWD);

      pr.fileEvent(kThreadId, EventType::READ, AT_FDCWD, "yoyo");
      CHECK(delegate.popFileEvent().path == kInitialPath + "/a_path/yoyo");
    }

    SECTION("UnknownThread") {
      pr.fileEvent(4329, EventType::READ, AT_FDCWD, "yoyo");
      CHECK(delegate.popFileEvent().path == "yoyo");
    }

    SECTION("AcrossThreads") {
      pr.newThread(kInitialPid, kThreadId, kThreadId3);
      pr.chdir(kThreadId, std::string(kNewPath), AT_FDCWD);

      pr.fileEvent(kThreadId3, EventType::READ, AT_FDCWD, "yoyo");
      CHECK(delegate.popFileEvent().path == kNewPath + "/yoyo");
    }

    SECTION("AcrossProcesses") {
      pr.chdir(kThreadId, std::string(kNewPath), AT_FDCWD);

      pr.fileEvent(kThreadId2, EventType::READ, AT_FDCWD, "yoyo");
      CHECK(delegate.popFileEvent().path == kInitialPath + "/yoyo");
    }

    SECTION("OnFork") {
      pr.newThread(kInitialPid + 2, kThreadId, kThreadId3);

      SECTION("ChdirInParent") {
        pr.chdir(kThreadId, std::string(kNewPath), AT_FDCWD);

        pr.fileEvent(kThreadId, EventType::READ, AT_FDCWD, "yoyo");
        CHECK(delegate.popFileEvent().path == kNewPath + "/yoyo");

        pr.fileEvent(kThreadId3, EventType::READ, AT_FDCWD, "yoyo");
        CHECK(delegate.popFileEvent().path == kInitialPath + "/yoyo");
      }

      SECTION("ChdirInChild") {
        pr.chdir(kThreadId3, std::string(kNewPath), AT_FDCWD);

        pr.fileEvent(kThreadId, EventType::READ, AT_FDCWD, "yoyo");
        CHECK(delegate.popFileEvent().path == kInitialPath + "/yoyo");

        pr.fileEvent(kThreadId3, EventType::READ, AT_FDCWD, "yoyo");
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
    pr.newThread(kInitialPid, kThreadId, kThreadId1);
    pr.threadChdir(kThreadId1, std::string(kNewPath), AT_FDCWD);
    pr.newThread(kInitialPid, kThreadId1, kThreadId2);

    SECTION("UnknownThread") {
      pr.threadChdir(65432, "hey_there", AT_FDCWD);
    }

    SECTION("SameThread") {
      pr.fileEvent(kThreadId1, EventType::READ, AT_FDCWD, "yoyo");
      CHECK(delegate.popFileEvent().path == kNewPath + "/yoyo");
    }

    SECTION("AcrossThreads") {
      pr.fileEvent(kThreadId2, EventType::READ, AT_FDCWD, "yoyo");
      CHECK(delegate.popFileEvent().path == kNewPath + "/yoyo");
    }

    SECTION("RelativeToFd") {
      pr.open(kThreadId1, kFd, AT_FDCWD, std::string(kFdPath), false);
      pr.threadChdir(kThreadId1, "a_path", kFd);

      pr.fileEvent(kThreadId1, EventType::READ, AT_FDCWD, "yoyo");
      CHECK(delegate.popFileEvent().path == kFdPath + "/a_path/yoyo");
    }

    SECTION("RelativeToCwd") {
      pr.threadChdir(kThreadId1, "a_path", AT_FDCWD);

      pr.fileEvent(kThreadId1, EventType::READ, AT_FDCWD, "yoyo");
      CHECK(delegate.popFileEvent().path == kNewPath + "/a_path/yoyo");
    }

    SECTION("NewThread") {
      SECTION("InheritThreadLocalCwd") {
        pr.fileEvent(kThreadId2, EventType::READ, AT_FDCWD, "yoyo");
        CHECK(delegate.popFileEvent().path == kNewPath + "/yoyo");
      }

      SECTION("ChdirInParent") {
        pr.threadChdir(kThreadId1, std::string(kNewerPath), AT_FDCWD);

        pr.fileEvent(kThreadId1, EventType::READ, AT_FDCWD, "yoyo");
        CHECK(delegate.popFileEvent().path == kNewerPath + "/yoyo");

        pr.fileEvent(kThreadId2, EventType::READ, AT_FDCWD, "yoyo");
        CHECK(delegate.popFileEvent().path == kNewPath + "/yoyo");
      }

      SECTION("ChdirInChild") {
        pr.threadChdir(kThreadId2, std::string(kNewerPath), AT_FDCWD);

        pr.fileEvent(kThreadId1, EventType::READ, AT_FDCWD, "yoyo");
        CHECK(delegate.popFileEvent().path == kNewPath + "/yoyo");

        pr.fileEvent(kThreadId2, EventType::READ, AT_FDCWD, "yoyo");
        CHECK(delegate.popFileEvent().path == kNewerPath + "/yoyo");
      }
    }
  }

  SECTION("Exec") {
    // Tested in the Open/Cloexec section

    SECTION("UnknownThread") {
      pr.exec(7652);
    }
  }
}

}  // namespace shk
