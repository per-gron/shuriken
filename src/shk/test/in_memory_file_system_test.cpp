#include <sys/stat.h>

#include <catch.hpp>

#include "in_memory_file_system.h"

namespace shk {

TEST_CASE("InMemoryFileSystem") {
  time_t now = 0;
  InMemoryFileSystem fs([&now] { return now; });
  const std::string abc = "abc";

  SECTION("lstat missing file") {
    const auto stat = fs.lstat("abc");
    CHECK(stat.result == ENOENT);
  }

  SECTION("stat missing file") {
    const auto stat = fs.stat("abc");
    CHECK(stat.result == ENOENT);
  }

  SECTION("mmap") {
    fs.writeFile("f", "contents");
    fs.mkdir("dir");
    CHECK_THROWS_AS(fs.mmap("nonexisting"), IoError);
    CHECK_THROWS_AS(fs.mmap("dir"), IoError);
    CHECK_THROWS_AS(fs.mmap("dir/nonexisting"), IoError);
    CHECK_THROWS_AS(fs.mmap("nonexisting/nonexisting"), IoError);
    CHECK(fs.mmap("f")->memory() == "contents");
  }

  SECTION("synonyms for root") {
    CHECK(fs.stat(".").result == 0);
    CHECK(fs.stat("/").result == 0);
    CHECK(fs.stat("a/..").result == 0);
  }

  SECTION("file mtime/ctime") {
    now = 1234;
    const auto stream = fs.open("f", "w");
    CHECK(fs.stat("f").timestamps.mtime == 1234);
    CHECK(fs.stat("f").timestamps.ctime == 1234);

    now++;
    stream->write(nullptr, 1, 0);
    CHECK(fs.stat("f").timestamps.mtime == 1235);
    CHECK(fs.stat("f").timestamps.ctime == 1235);
  }

  SECTION("directory mtime/ctime") {
    now = 123;
    fs.mkdir("d");
    fs.mkdir("d/subdir");
    CHECK(fs.stat("d").timestamps.mtime == 123);
    CHECK(fs.stat("d").timestamps.ctime == 123);

    now++;
    fs.open("d/f.txt", "w");
    CHECK(fs.stat("d").timestamps.mtime == 124);
    CHECK(fs.stat("d").timestamps.ctime == 124);

    now++;
    fs.unlink("d/f.txt");
    CHECK(fs.stat("d").timestamps.mtime == 125);
    CHECK(fs.stat("d").timestamps.ctime == 125);

    now++;
    fs.rmdir("d/subdir");
    CHECK(fs.stat("d").timestamps.mtime == 126);
    CHECK(fs.stat("d").timestamps.ctime == 126);
  }

  SECTION("mkdir") {
    fs.mkdir(abc);

    const auto stat = fs.stat(abc);
    CHECK(stat.result == 0);
    CHECK(S_ISDIR(stat.metadata.mode));
  }

  SECTION("mkdir over existing directory") {
    fs.mkdir(abc);
    CHECK_THROWS_AS(fs.mkdir(abc), IoError);
  }

  SECTION("rmdir missing file") {
    CHECK_THROWS_AS(fs.rmdir(abc), IoError);
  }

  SECTION("rmdir") {
    fs.mkdir(abc);
    fs.rmdir(abc);

    CHECK(fs.stat(abc).result == ENOENT);
  }

  SECTION("rmdir nonempty directory") {
    const std::string path = "abc";
    const std::string file_path = "abc/def";
    fs.mkdir(path);
    fs.open(file_path, "w");
    CHECK_THROWS_AS(fs.rmdir(path), IoError);
    CHECK(fs.stat(path).result == 0);
  }

  SECTION("unlink directory") {
    fs.mkdir(abc);
    CHECK_THROWS_AS(fs.unlink(abc), IoError);
  }

  SECTION("unlink") {
    fs.open(abc, "w");

    fs.unlink(abc);
    CHECK(fs.stat(abc).result == ENOENT);
  }

  SECTION("symlink") {
    fs.symlink("target", "link");
    const auto stat = fs.lstat("link");
    CHECK(stat.result != ENOENT);
    CHECK(S_ISLNK(stat.metadata.mode));
  }

  SECTION("rename") {
    SECTION("missing file") {
      CHECK_THROWS_AS(fs.rename("a", "b"), IoError);
      CHECK_THROWS_AS(fs.rename("a/b", "b"), IoError);
      CHECK_THROWS_AS(fs.rename("a", "b/a"), IoError);
    }

    SECTION("directory") {
      fs.mkdir("a");
      fs.open("a/file", "w");
      fs.rename("a", "b");
      CHECK(fs.stat("a").result == ENOENT);
      CHECK(fs.stat("b").result == 0);
      CHECK(fs.readFile("b/file") == "");
    }

    SECTION("directory with same name") {
      fs.mkdir("a");
      fs.rename("a", "a");
      CHECK(fs.stat("a").result == 0);
    }

    SECTION("file") {
      fs.open("a", "w");
      fs.rename("a", "b");
      CHECK(fs.stat("a").result == ENOENT);
      CHECK(fs.readFile("b") == "");
    }

    SECTION("update directory mtime") {
      fs.mkdir("a");
      fs.mkdir("b");
      fs.open("a/a", "w");
      now = 123;
      fs.rename("a/a", "b/b");
      CHECK(fs.stat("a").timestamps.mtime == 123);
      CHECK(fs.stat("a").timestamps.ctime == 123);
      CHECK(fs.stat("b").timestamps.mtime == 123);
      CHECK(fs.stat("b").timestamps.ctime == 123);
    }

    SECTION("file with same name") {
      fs.open("a", "w");
      fs.rename("a", "a");
      CHECK(fs.stat("a").result == 0);
      CHECK(fs.readFile("a") == "");
    }

    SECTION("overwrite file with file") {
      fs.writeFile("a", "a!");
      fs.writeFile("b", "b!");
      fs.rename("a", "b");
      CHECK(fs.stat("a").result == ENOENT);
      CHECK(fs.readFile("b") == "a!");
    }

    SECTION("overwrite directory with file") {
      fs.open("a", "w");
      fs.mkdir("b");
      CHECK_THROWS_AS(fs.rename("a", "b"), IoError);
    }

    SECTION("overwrite file with directory") {
      fs.mkdir("a");
      fs.open("b", "w");
      CHECK_THROWS_AS(fs.rename("a", "b"), IoError);
    }

    SECTION("overwrite directory with directory") {
      fs.mkdir("a");
      fs.open("a/b", "w");
      fs.mkdir("b");
      fs.rename("a", "b");
      CHECK(fs.stat("a/b").result == ENOTDIR);
      CHECK(fs.stat("a").result == ENOENT);
      CHECK(fs.stat("b").result == 0);
      CHECK(fs.stat("b/b").result == 0);
    }

    SECTION("overwrite directory with nonempty directory") {
      fs.mkdir("a");
      fs.mkdir("b");
      fs.open("b/b", "w");
      CHECK_THROWS_AS(fs.rename("a", "b"), IoError);
    }
  }

  SECTION("truncate") {
    fs.mkdir("dir");
    fs.writeFile("file", "sweet bananas!");
    CHECK_THROWS_AS(fs.truncate("dir", 0), IoError);
    CHECK_THROWS_AS(fs.truncate("missing", 0), IoError);
    CHECK_THROWS_AS(fs.truncate("dir/missing", 0), IoError);
    CHECK_THROWS_AS(fs.truncate("missing/a", 0), IoError);

    fs.truncate("file", 5);
    CHECK(fs.readFile("file") == "sweet");
  }

  SECTION("readDir") {
    SECTION("success") {
      fs.mkdir("d");
      fs.open("d/a", "w");
      fs.mkdir("d/b");
      auto dir_entries = fs.readDir("d");
      std::sort(dir_entries.begin(), dir_entries.end());
      REQUIRE(dir_entries.size() == 2);
      CHECK(dir_entries[0].type == DirEntry::Type::REG);
      CHECK(dir_entries[0].name == "a");
      CHECK(dir_entries[1].type == DirEntry::Type::DIR);
      CHECK(dir_entries[1].name == "b");
    }

    SECTION("fail") {
      fs.open("f", "w");
      fs.mkdir("d");
      CHECK_THROWS_AS(fs.readDir("f"), IoError);
      CHECK_THROWS_AS(fs.readDir("f/x"), IoError);
      CHECK_THROWS_AS(fs.readDir("nonexisting"), IoError);
      CHECK_THROWS_AS(fs.readDir("d/nonexisting"), IoError);
    }
  }

  SECTION("readSymlink") {
    fs.symlink("target", "link");
    CHECK(fs.readSymlink("link") == "target");
  }

  SECTION("open with bad mode") {
    CHECK_THROWS_AS(fs.open(abc, ""), IoError);
  }

  SECTION("open for writing") {
    fs.open(abc, "w");

    const auto stat = fs.stat(abc);
    CHECK(stat.result == 0);
    CHECK(S_ISREG(stat.metadata.mode));
  }

  SECTION("open for appending") {
    fs.writeFile(abc, "swe");
    {
      const auto stream = fs.open(abc, "ab");
      const std::string et = "et";
      stream->write(reinterpret_cast<const uint8_t *>(et.data()), et.size(), 1);
    }

    CHECK(fs.readFile(abc) == "sweet");
  }

  SECTION("open new file for appending") {
    {
      const auto stream = fs.open(abc, "ab");
      const std::string et = "et";
      stream->write(reinterpret_cast<const uint8_t *>(et.data()), et.size(), 1);
    }

    CHECK(fs.readFile(abc) == "et");
  }

  SECTION("open for writing in binary") {
    fs.open(abc, "wb");

    const auto stat = fs.stat(abc);
    CHECK(stat.result == 0);
    CHECK(S_ISREG(stat.metadata.mode));
  }

  SECTION("open missing file for reading") {
    CHECK_THROWS_AS(fs.open("abc", "r"), IoError);
  }

  SECTION("inos are unique") {
    fs.open("1", "w");
    fs.open("2", "w");
    fs.mkdir("3");
    fs.mkdir("4");

    std::unordered_set<ino_t> inos;
    inos.insert(fs.stat("1").metadata.ino);
    inos.insert(fs.stat("2").metadata.ino);
    inos.insert(fs.stat("3").metadata.ino);
    inos.insert(fs.stat("4").metadata.ino);
    CHECK(inos.size() == 4);
  }

  SECTION("hashFile") {
    fs.writeFile("one", "some_content");
    fs.writeFile("two", "some_content");
    fs.writeFile("three", "some_other_content");

    CHECK(fs.hashFile("one") == fs.hashFile("one"));
    CHECK(fs.hashFile("one") == fs.hashFile("two"));
    CHECK(fs.hashFile("one") != fs.hashFile("three"));
  }

  SECTION("mkstemp creates file") {
    const auto path = fs.mkstemp("hi.XXX");
    CHECK(fs.stat(path).result == 0);
  }

  SECTION("mkstemp creates unique paths") {
    const auto path1 = fs.mkstemp("hi.XXX");
    const auto path2 = fs.mkstemp("hi.XXX");
    CHECK(path1 != path2);
    CHECK(fs.stat(path1).result == 0);
    CHECK(fs.stat(path2).result == 0);
  }

  SECTION("enqueueMkstempResult") {
    SECTION("one path") {
      fs.enqueueMkstempResult("one");
      CHECK(fs.mkstemp("hi.XXX") == "one");
      CHECK(fs.stat("one").result == ENOENT);
    }

    SECTION("two paths") {
      fs.enqueueMkstempResult("one");
      fs.enqueueMkstempResult("two");
      CHECK(fs.mkstemp("hi.XXX") == "one");
      CHECK(fs.stat("one").result == ENOENT);
      CHECK(fs.mkstemp("hi.XXX") == "two");
      CHECK(fs.stat("two").result == ENOENT);
    }
  }
}

}  // namespace shk
