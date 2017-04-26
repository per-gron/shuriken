#include <catch.hpp>

#include <sys/stat.h>

#include "fs/fingerprint.h"

#include "../in_memory_file_system.h"

namespace shk {
namespace {

Hash hashFile(FileSystem &file_system, nt_string_view path) {
  Hash hash;
  bool success;
  std::string err;
  std::tie(hash, success) = file_system.hashFile(path, &err);
  CHECK(success);
  CHECK(err == "");
  return hash;
}

Hash hashSymlink(FileSystem &file_system, nt_string_view path) {
  Hash hash;
  bool success;
  std::string err;
  std::tie(hash, success) = file_system.hashSymlink(path, &err);
  CHECK(success);
  CHECK(err == "");
  return hash;
}

}  // anonymous namespace

TEST_CASE("Fingerprint") {
  time_t now = 321;
  InMemoryFileSystem fs([&now] { return now; });
  const std::string initial_contents = "initial_contents";
  fs.writeFile("a", initial_contents);
  fs.mkdir("dir");
  fs.symlink("target", "link");

  SECTION("computeFingerprintHash") {
    const Hash zero{};
    Hash ans_hash;

    SECTION("directory") {
      detail::computeFingerprintHash(fs, S_IFDIR, "dir", &ans_hash);
      CHECK(ans_hash == fs.hashDir("dir"));
    }

    SECTION("link") {
      detail::computeFingerprintHash(fs, S_IFLNK, "link", &ans_hash);
      CHECK(ans_hash == hashSymlink(fs, "link"));
    }

    SECTION("regular file") {
      detail::computeFingerprintHash(fs, S_IFREG, "a", &ans_hash);
      CHECK(ans_hash == hashFile(fs, "a"));
    }

    SECTION("missing file") {
      detail::computeFingerprintHash(fs, 0, "a", &ans_hash);
      CHECK(ans_hash == zero);
    }

    SECTION("other") {
      detail::computeFingerprintHash(fs, S_IFBLK, "a", &ans_hash);
      CHECK(ans_hash == zero);
    }
  }

  SECTION("Stat") {
    Fingerprint::Stat a;
    a.size = 1;
    a.ino = 2;
    a.mode = 3;
    a.mtime = 4;
    a.ctime = 5;
    Fingerprint::Stat b = a;

    SECTION("fromStat") {
      SECTION("missing file") {
        auto stat = fs.stat("missing");
        Fingerprint::Stat fp_stat;
        Fingerprint::Stat::fromStat(stat, &fp_stat);
        CHECK(fp_stat == Fingerprint::Stat());
      }

      SECTION("directory") {
        auto stat = fs.stat("dir");
        Fingerprint::Stat fp_stat;
        Fingerprint::Stat::fromStat(stat, &fp_stat);

        CHECK(fp_stat.size == 0);
        CHECK(fp_stat.ino == stat.metadata.ino);
        CHECK(S_ISDIR(fp_stat.mode));
        CHECK(fp_stat.mtime == stat.timestamps.mtime);
        CHECK(fp_stat.ctime == stat.timestamps.ctime);
      }

      SECTION("file") {
        auto stat = fs.stat("a");
        Fingerprint::Stat fp_stat;
        Fingerprint::Stat::fromStat(stat, &fp_stat);

        CHECK(fp_stat.size == stat.metadata.size);
        CHECK(fp_stat.ino == stat.metadata.ino);
        CHECK(S_ISREG(fp_stat.mode));
        CHECK(fp_stat.mtime == stat.timestamps.mtime);
        CHECK(fp_stat.ctime == stat.timestamps.ctime);
      }
    }

    SECTION("equal") {
      CHECK(a == b);
      CHECK(!(a != b));
      CHECK(!(a < b || b < a));
    }

    SECTION("size") {
      b.size++;
      CHECK(a != b);
      CHECK(!(a == b));
      CHECK((a < b || b < a));
    }

    SECTION("ino") {
      b.ino++;
      CHECK(a != b);
      CHECK(!(a == b));
      CHECK((a < b || b < a));
    }

    SECTION("mode") {
      b.mode++;
      CHECK(a != b);
      CHECK(!(a == b));
      CHECK((a < b || b < a));
    }

    SECTION("mtime") {
      b.mtime++;
      CHECK(a != b);
      CHECK(!(a == b));
      CHECK((a < b || b < a));
    }

    SECTION("ctime") {
      b.ctime++;
      CHECK(a != b);
      CHECK(!(a == b));
      CHECK((a < b || b < a));
    }
  }

  SECTION("Fingerprint") {
    Fingerprint a;
    a.stat.size = 1;
    a.racily_clean = true;
    std::fill(a.hash.data.begin(), a.hash.data.end(), 0);

    auto b = a;

    SECTION("equal") {
      CHECK(a == b);
      CHECK(!(a != b));
      CHECK(!(a < b || b < a));
    }

    SECTION("stat") {
      b.stat.size++;
      CHECK(a != b);
      CHECK(!(a == b));
      CHECK((a < b || b < a));
    }

    SECTION("racily_clean") {
      b.racily_clean = !b.racily_clean;
      CHECK(a != b);
      CHECK(!(a == b));
      CHECK((a < b || b < a));
    }

    SECTION("hash") {
      b.hash.data[0] = 1;
      CHECK(a != b);
      CHECK(!(a == b));
      CHECK((a < b || b < a));
    }
  }

  SECTION("takeFingerprint") {
    SECTION("regular file") {
      Fingerprint fp;
      FileId file_id;
      std::tie(fp, file_id) = takeFingerprint(fs, now + 1, "a");

      CHECK(file_id == FileId(fs.lstat("a")));

      CHECK(fp.stat.size == initial_contents.size());
      CHECK(fp.stat.ino == fs.stat("a").metadata.ino);
      CHECK(S_ISREG(fp.stat.mode));
      CHECK(fp.stat.mtime == now);
      CHECK(fp.stat.ctime == now);
      CHECK(fp.racily_clean == false);
      CHECK(fp.hash == hashFile(fs, "a"));
      CHECK(fp.stat.couldAccess());
      CHECK(!fp.stat.isDir());
    }

    SECTION("missing file") {
      Fingerprint fp;
      FileId file_id;
      std::tie(fp, file_id) = takeFingerprint(fs, now + 1, "b");

      CHECK(file_id == FileId(fs.lstat("b")));

      CHECK(fp.stat.size == 0);
      CHECK(fp.stat.ino == 0);
      CHECK(fp.stat.mode == 0);
      CHECK(fp.stat.mtime == 0);
      CHECK(fp.stat.ctime == 0);
      CHECK(fp.racily_clean == false);
      Hash zero;
      std::fill(zero.data.begin(), zero.data.end(), 0);
      CHECK(fp.hash == zero);
      CHECK(!fp.stat.couldAccess());
      CHECK(!fp.stat.isDir());
    }

    SECTION("directory") {
      Fingerprint fp;
      FileId file_id;
      std::tie(fp, file_id) = takeFingerprint(fs, now + 1, "dir");

      CHECK(file_id == FileId(fs.lstat("dir")));

      CHECK(fp.stat.size == 0);
      CHECK(fp.stat.ino == fs.stat("dir").metadata.ino);
      CHECK(S_ISDIR(fp.stat.mode));
      CHECK(fp.stat.mtime == now);
      CHECK(fp.stat.ctime == now);
      CHECK(fp.racily_clean == false);
      CHECK(fp.hash == fs.hashDir("dir"));
      CHECK(fp.stat.couldAccess());
      CHECK(fp.stat.isDir());
    }

    SECTION("symlink") {
      Fingerprint fp;
      FileId file_id;
      std::tie(fp, file_id) = takeFingerprint(fs, now + 1, "link");

      CHECK(file_id == FileId(fs.lstat("link")));

      CHECK(fp.stat.size == fs.readSymlink("link").size());
      CHECK(fp.stat.ino == fs.lstat("link").metadata.ino);
      CHECK(S_ISLNK(fp.stat.mode));
      CHECK(fp.stat.mtime == now);
      CHECK(fp.stat.ctime == now);
      CHECK(fp.racily_clean == false);
      CHECK(fp.hash == hashSymlink(fs, "link"));
      CHECK(fp.stat.couldAccess());
      CHECK(!fp.stat.isDir());
    }

    SECTION("racily clean") {
      Fingerprint fp;
      FileId file_id;
      std::tie(fp, file_id) = takeFingerprint(fs, now, "a");

      CHECK(file_id == FileId(fs.lstat("a")));
      CHECK(fp.racily_clean == true);
    }

    SECTION("racily clean in the future") {
      Fingerprint fp;
      FileId file_id;
      std::tie(fp, file_id) = takeFingerprint(fs, now - 1, "a");

      CHECK(file_id == FileId(fs.lstat("a")));
      CHECK(fp.racily_clean == true);
    }
  }

  SECTION("retakeFingerprint") {
    SECTION("matching old_fingerprint (missing file)") {
      Fingerprint fp;
      FileId file_id;
      std::tie(fp, file_id) = takeFingerprint(fs, now, "nonexisting");
      now++;
      Fingerprint new_fp;
      FileId new_file_id;
      std::tie(new_fp, new_file_id) =
          retakeFingerprint(fs, now, "nonexisting", fp);
      CHECK(fp == new_fp);

      CHECK(new_file_id == FileId(fs.lstat("nonexisting")));
    }

    SECTION("matching old_fingerprint (file)") {
      fs.writeFile("b", "data");
      now++;
      Fingerprint fp;
      FileId file_id;
      std::tie(fp, file_id) = takeFingerprint(fs, now, "b");
      now++;
      Fingerprint new_fp;
      FileId new_file_id;
      std::tie(new_fp, new_file_id) = retakeFingerprint(fs, now, "b", fp);
      CHECK(fp == new_fp);

      CHECK(new_file_id == FileId(fs.lstat("b")));
    }

    SECTION("matching old_fingerprint (file, should_update)") {
      fs.writeFile("b", "data");
      Fingerprint fp;
      FileId file_id;
      std::tie(fp, file_id) = takeFingerprint(fs, now, "b");
      now++;
      Fingerprint new_fp;
      FileId new_file_id;
      std::tie(new_fp, new_file_id) = retakeFingerprint(fs, now, "b", fp);
      CHECK(fp != new_fp);
      CHECK(
          std::make_pair(new_fp, new_file_id) == takeFingerprint(fs, now, "b"));

      CHECK(new_file_id == FileId(fs.lstat("b")));
    }

    SECTION("matching old_fingerprint (dir)") {
      now++;
      Fingerprint fp;
      FileId file_id;
      std::tie(fp, file_id) = takeFingerprint(fs, now, "dir");
      now++;
      Fingerprint new_fp;
      FileId new_file_id;
      std::tie(new_fp, new_file_id) = retakeFingerprint(fs, now, "dir", fp);
      CHECK(fp == new_fp);

      CHECK(new_file_id == FileId(fs.lstat("dir")));
    }

    SECTION("matching old_fingerprint (dir, should_update)") {
      Fingerprint fp;
      FileId file_id;
      std::tie(fp, file_id) = takeFingerprint(fs, now, "dir");
      now++;
      Fingerprint new_fp;
      FileId new_file_id;
      std::tie(new_fp, new_file_id) = retakeFingerprint(fs, now, "dir", fp);
      CHECK(fp != new_fp);
      CHECK(
          std::make_pair(new_fp, new_file_id) ==
          takeFingerprint(fs, now, "dir"));

      CHECK(new_file_id == FileId(fs.lstat("dir")));
    }

    SECTION("not matching old_fingerprint") {
      Fingerprint fp;
      FileId file_id;
      std::tie(fp, file_id) = takeFingerprint(fs, now, "a");
      fs.writeFile("a", "data");
      now++;
      Fingerprint new_fp;
      FileId new_file_id;
      std::tie(new_fp, new_file_id) = retakeFingerprint(fs, now, "a", fp);
      CHECK(fp != new_fp);
      CHECK(
          std::make_pair(new_fp, new_file_id) == takeFingerprint(fs, now, "a"));

      CHECK(new_file_id == FileId(fs.lstat("a")));
    }
  }

  SECTION("MatchesResult") {
    MatchesResult result;

    SECTION("equal") {
      CHECK(result == MatchesResult());
      CHECK(!(result != MatchesResult()));
    }

    SECTION("clean") {
      result.clean = !result.clean;
      CHECK(!(result == MatchesResult()));
      CHECK(result != MatchesResult());
    }

    SECTION("should_update") {
      result.should_update = !result.should_update;
      CHECK(!(result == MatchesResult()));
      CHECK(result != MatchesResult());
    }
  }

  SECTION("fingerprintMatches (stating variant)") {
    SECTION("no changes, fingerprint taken at the same time as file mtime") {
      Fingerprint initial_fp;
      FileId file_id;
      std::tie(initial_fp, file_id) = takeFingerprint(fs, now, "a");
      const auto result = fingerprintMatches(fs, "a", initial_fp);
      CHECK(result.clean);
      CHECK(result.should_update);
    }

    SECTION("no changes, fingerprint taken later") {
      Fingerprint initial_fp;
      FileId file_id;
      std::tie(initial_fp, file_id) = takeFingerprint(fs, now + 1, "a");
      const auto result = fingerprintMatches(fs, "a", initial_fp);
      CHECK(result.clean);
      CHECK(!result.should_update);
    }

    SECTION("file changed, everything at the same time, same size") {
      Fingerprint initial_fp;
      FileId file_id;
      std::tie(initial_fp, file_id) = takeFingerprint(fs, now, "a");
      fs.writeFile("a", "initial_content>");
      const auto result = fingerprintMatches(fs, "a", initial_fp);
      CHECK(!result.clean);
      CHECK(result.should_update);
    }

    SECTION("file changed, everything at the same time, different size") {
      Fingerprint initial_fp;
      FileId file_id;
      std::tie(initial_fp, file_id) = takeFingerprint(fs, now, "a");
      fs.writeFile("a", "changed");
      const auto result = fingerprintMatches(fs, "a", initial_fp);
      CHECK(!result.clean);
      // It can see that the file size is different so no need to re-hash and
      // thus no need to update.
      CHECK(!result.should_update);
    }

    SECTION("file changed, including timestamps, same size") {
      Fingerprint initial_fp;
      FileId file_id;
      std::tie(initial_fp, file_id) = takeFingerprint(fs, now, "a");
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
      Fingerprint initial_fp;
      FileId file_id;
      std::tie(initial_fp, file_id) = takeFingerprint(fs, now, "a");
      now++;
      fs.writeFile("a", "changed");
      const auto result = fingerprintMatches(fs, "a", initial_fp);
      CHECK(!result.clean);
      // It can see that the file size is different so no need to re-hash and
      // thus no need to update.
      CHECK(!result.should_update);
    }

    SECTION("only timestamps changed") {
      Fingerprint initial_fp;
      FileId file_id;
      std::tie(initial_fp, file_id) = takeFingerprint(fs, now, "a");
      now++;
      fs.writeFile("a", initial_contents);
      const auto result = fingerprintMatches(fs, "a", initial_fp);
      CHECK(result.clean);
      CHECK(result.should_update);
    }

    SECTION("missing file before and after") {
      Fingerprint initial_fp;
      FileId file_id;
      std::tie(initial_fp, file_id) = takeFingerprint(fs, now, "b");
      const auto result = fingerprintMatches(fs, "b", initial_fp);
      CHECK(result.clean);
      CHECK(!result.should_update);
    }

    SECTION("missing file before and after, zero timestamp") {
      Fingerprint initial_fp;
      FileId file_id;
      std::tie(initial_fp, file_id) = takeFingerprint(fs, 0, "b");
      const auto result = fingerprintMatches(fs, "b", initial_fp);
      CHECK(result.clean);
      CHECK(!result.should_update);
    }

    SECTION("missing file before but not after") {
      Fingerprint initial_fp;
      FileId file_id;
      std::tie(initial_fp, file_id) = takeFingerprint(fs, now, "b");
      fs.writeFile("b", initial_contents);
      const auto result = fingerprintMatches(fs, "b", initial_fp);
      CHECK(!result.clean);
      CHECK(!result.should_update);
    }

    SECTION("missing file after but not before") {
      Fingerprint initial_fp;
      FileId file_id;
      std::tie(initial_fp, file_id) = takeFingerprint(fs, now, "a");
      fs.unlink("a");
      const auto result = fingerprintMatches(fs, "a", initial_fp);
      CHECK(!result.clean);
      CHECK(!result.should_update);
    }

    SECTION(
        "dir: no changes, fingerprint taken at the same time as file mtime") {
      fs.mkdir("d");
      Fingerprint initial_fp;
      FileId file_id;
      std::tie(initial_fp, file_id) = takeFingerprint(fs, now, "d");
      const auto result = fingerprintMatches(fs, "d", initial_fp);
      CHECK(result.clean);
      CHECK(result.should_update);
    }
  }

  SECTION("fingerprintMatches (non-stating variant)") {
    static const char *kPaths[] = { "a", "dir", "missing" };
    static const char *kPathsNoMissing[] = { "a", "dir" };

    SECTION("match") {
      for (const char *path : kPaths) {
        const auto fingerprint = takeFingerprint(fs, now, path).first;

        const auto new_stat = fs.lstat(path);

        const auto new_hash =
            std::string(path) == "dir" ? fs.hashDir(path) :
            std::string(path) == "missing" ? Hash{} :
            hashFile(fs, path);

        CHECK(fingerprintMatches(fingerprint, new_stat, new_hash));
      }
    }

    SECTION("size mismatch") {
      for (const char *path : kPathsNoMissing) {
        const auto fingerprint = takeFingerprint(fs, now, path).first;

        auto new_stat = fs.lstat(path);
        new_stat.metadata.size++;

        const auto new_hash =
            std::string(path) == "dir" ? fs.hashDir(path) :
            hashFile(fs, path);

        CHECK(!fingerprintMatches(fingerprint, new_stat, new_hash));
      }
    }

    SECTION("mode mismatch") {
      for (const char *path : kPathsNoMissing) {
        const auto fingerprint = takeFingerprint(fs, now, path).first;

        auto new_stat = fs.lstat(path);
        new_stat.metadata.mode++;

        const auto new_hash =
            std::string(path) == "dir" ? fs.hashDir(path) :
            hashFile(fs, path);

        CHECK(!fingerprintMatches(fingerprint, new_stat, new_hash));
      }
    }

    SECTION("hash mismatch") {
      for (const char *path : kPaths) {
        const auto fingerprint = takeFingerprint(fs, now, path).first;

        const auto new_stat = fs.lstat(path);

        auto new_hash =
            std::string(path) == "dir" ? fs.hashDir(path) :
            std::string(path) == "missing" ? Hash{} :
            hashFile(fs, path);

        new_hash.data[0]++;

        CHECK(!fingerprintMatches(fingerprint, new_stat, new_hash));
      }
    }
  }
}

}  // namespace shk
