#include <catch.hpp>

#include <sys/stat.h>

#include "fingerprint.h"

#include "in_memory_file_system.h"

namespace shk {

TEST_CASE("Fingerprint") {
  time_t now = 321;
  InMemoryFileSystem fs([&now] { return now; });
  const std::string initial_contents = "initial_contents";
  writeFile(fs, "a", initial_contents);
  fs.mkdir("dir");

  SECTION("Stat") {
    Fingerprint::Stat a;
    a.size = 1;
    a.ino = 2;
    a.mode = 3;
    a.mtime = 4;
    a.ctime = 5;
    Fingerprint::Stat b = a;

    SECTION("equal") {
      CHECK(a == b);
      CHECK(!(a != b));
    }

    SECTION("size") {
      b.size++;
      CHECK(a != b);
      CHECK(!(a == b));
    }

    SECTION("ino") {
      b.ino++;
      CHECK(a != b);
      CHECK(!(a == b));
    }

    SECTION("mode") {
      b.mode++;
      CHECK(a != b);
      CHECK(!(a == b));
    }

    SECTION("mtime") {
      b.mtime++;
      CHECK(a != b);
      CHECK(!(a == b));
    }

    SECTION("ctime") {
      b.ctime++;
      CHECK(a != b);
      CHECK(!(a == b));
    }
  }

  SECTION("takeFingerprint") {
    SECTION("regular file") {
      const auto fp = takeFingerprint(fs, [] { return 12345; }, "a");

      CHECK(fp.stat.size == initial_contents.size());
      CHECK(fp.stat.ino == fs.stat("a").metadata.ino);
      CHECK(S_ISREG(fp.stat.mode));
      CHECK(fp.stat.mtime == now);
      CHECK(fp.stat.ctime == now);
      CHECK(fp.timestamp == 12345);
      CHECK(fp.hash == fs.hashFile("a"));
    }

    SECTION("directory") {
      const auto fp = takeFingerprint(fs, [] { return 12345; }, "dir");

      CHECK(fp.stat.size == 0);
      CHECK(fp.stat.ino == fs.stat("dir").metadata.ino);
      CHECK(S_ISDIR(fp.stat.mode));
      CHECK(fp.stat.mtime == now);
      CHECK(fp.stat.ctime == now);
      CHECK(fp.timestamp == 12345);
      CHECK(fp.hash == fs.hashDir("dir"));
    }
  }
}

}  // namespace shk
