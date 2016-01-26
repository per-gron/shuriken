#include <catch.hpp>

#include <sys/stat.h>

#include "fingerprint.h"

#include "in_memory_file_system.h"

namespace shk {

TEST_CASE("Fingerprint") {
  time_t now = 321;
  InMemoryFileSystem fs([&now] { return now; });
  const std::string initial_contents = "initial_contents";
  fs.writeFile("a", initial_contents);
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

  SECTION("Fingerprint") {
    Fingerprint a;
    a.stat.size = 1;
    a.timestamp = 2;
    std::fill(a.hash.data.begin(), a.hash.data.end(), 0);

    auto b = a;

    SECTION("equal") {
      CHECK(a == b);
      CHECK(!(a != b));
    }

    SECTION("stat") {
      b.stat.size++;
      CHECK(a != b);
      CHECK(!(a == b));
    }

    SECTION("timestamp") {
      b.timestamp++;
      CHECK(a != b);
      CHECK(!(a == b));
    }

    SECTION("hash") {
      b.hash.data[0] = 1;
      CHECK(a != b);
      CHECK(!(a == b));
    }
  }

  SECTION("takeFingerprint") {
    SECTION("regular file") {
      const auto fp = takeFingerprint(fs, 12345, "a");

      CHECK(fp.stat.size == initial_contents.size());
      CHECK(fp.stat.ino == fs.stat("a").metadata.ino);
      CHECK(S_ISREG(fp.stat.mode));
      CHECK(fp.stat.mtime == now);
      CHECK(fp.stat.ctime == now);
      CHECK(fp.timestamp == 12345);
      CHECK(fp.hash == fs.hashFile("a"));
      CHECK(fp.stat.couldAccess());
    }

    SECTION("missing file") {
      const auto fp = takeFingerprint(fs, 12345, "b");

      CHECK(fp.stat.size == 0);
      CHECK(fp.stat.ino == 0);
      CHECK(fp.stat.mode == 0);
      CHECK(fp.stat.mtime == 0);
      CHECK(fp.stat.ctime == 0);
      CHECK(fp.timestamp == 12345);
      Hash zero;
      std::fill(zero.data.begin(), zero.data.end(), 0);
      CHECK(fp.hash == zero);
      CHECK(!fp.stat.couldAccess());
    }

    SECTION("directory") {
      const auto fp = takeFingerprint(fs, 12345, "dir");

      CHECK(fp.stat.size == 0);
      CHECK(fp.stat.ino == fs.stat("dir").metadata.ino);
      CHECK(S_ISDIR(fp.stat.mode));
      CHECK(fp.stat.mtime == now);
      CHECK(fp.stat.ctime == now);
      CHECK(fp.timestamp == 12345);
      CHECK(fp.hash == fs.hashDir("dir"));
      CHECK(fp.stat.couldAccess());
    }
  }

  SECTION("fingerprintMatches") {
    SECTION("no changes, fingerprint taken at the same time as file mtime") {
      const auto initial_fp = takeFingerprint(fs, now, "a");
      const auto result = fingerprintMatches(fs, "a", initial_fp);
      CHECK(result.clean);
      CHECK(result.should_update);
    }

    SECTION("no changes, fingerprint taken later") {
      const auto initial_fp = takeFingerprint(fs, now + 1, "a");
      const auto result = fingerprintMatches(fs, "a", initial_fp);
      CHECK(result.clean);
      CHECK(!result.should_update);
    }

    SECTION("file changed, everything at the same time, same size") {
      const auto initial_fp = takeFingerprint(fs, now, "a");
      fs.writeFile("a", "initial_content>");
      const auto result = fingerprintMatches(fs, "a", initial_fp);
      CHECK(!result.clean);
      CHECK(result.should_update);
    }

    SECTION("file changed, everything at the same time, different size") {
      const auto initial_fp = takeFingerprint(fs, now, "a");
      fs.writeFile("a", "changed");
      const auto result = fingerprintMatches(fs, "a", initial_fp);
      CHECK(!result.clean);
      // It can see that the file size is different so no need to re-hash and
      // thus no need to update.
      CHECK(!result.should_update);
    }

    SECTION("file changed, including timestamps, same size") {
      const auto initial_fp = takeFingerprint(fs, now, "a");
      now++;
      fs.writeFile("a", "initial_content>");
      const auto result = fingerprintMatches(fs, "a", initial_fp);
      CHECK(!result.clean);
      // It can see that the file's timestamp is newer than the fingerprint,
      // but it needs to hash the contents to find out if it is actually
      // different.
      CHECK(result.should_update);
    }

    SECTION("file changed, including timestamps, different size") {
      const auto initial_fp = takeFingerprint(fs, now, "a");
      now++;
      fs.writeFile("a", "changed");
      const auto result = fingerprintMatches(fs, "a", initial_fp);
      CHECK(!result.clean);
      // It can see that the file size is different so no need to re-hash and
      // thus no need to update.
      CHECK(!result.should_update);
    }

    SECTION("only timestamps changed") {
      const auto initial_fp = takeFingerprint(fs, now, "a");
      now++;
      fs.writeFile("a", initial_contents);
      const auto result = fingerprintMatches(fs, "a", initial_fp);
      CHECK(result.clean);
      CHECK(result.should_update);
    }

    SECTION("missing file before and after") {
      const auto initial_fp = takeFingerprint(fs, now, "b");
      const auto result = fingerprintMatches(fs, "b", initial_fp);
      CHECK(result.clean);
      CHECK(!result.should_update);
    }

    SECTION("missing file before but not after") {
      const auto initial_fp = takeFingerprint(fs, now, "b");
      fs.writeFile("b", initial_contents);
      const auto result = fingerprintMatches(fs, "b", initial_fp);
      CHECK(!result.clean);
      CHECK(!result.should_update);
    }

    SECTION("missing file after but not before") {
      const auto initial_fp = takeFingerprint(fs, now, "a");
      fs.unlink("a");
      const auto result = fingerprintMatches(fs, "a", initial_fp);
      CHECK(!result.clean);
      CHECK(!result.should_update);
    }

    SECTION("dir: no changes, fingerprint taken at the same time as file mtime") {
      fs.mkdir("d");
      const auto initial_fp = takeFingerprint(fs, now, "d");
      const auto result = fingerprintMatches(fs, "d", initial_fp);
      CHECK(result.clean);
      CHECK(result.should_update);
    }
  }
}

}  // namespace shk
