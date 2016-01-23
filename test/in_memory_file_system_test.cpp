#include <sys/stat.h>

#include <catch.hpp>

#include "in_memory_file_system.h"
#include "generators.h"

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

  SECTION("open for writing") {
    fs.open(abc, "w");

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

  SECTION("writeFile") {
    writeFile(fs, abc, "hello");
    CHECK(fs.stat(abc).result == 0);  // Verify file exists
  }

  SECTION("writeFile, readFile") {
    writeFile(fs, abc, "hello");
    CHECK(fs.readFile(abc) == "hello");
  }

  SECTION("writeFile, writeFile, readFile") {
    writeFile(fs, abc, "hello");
    writeFile(fs, abc, "hello!");
    CHECK(fs.readFile(abc) == "hello!");
  }

  SECTION("hashFile") {
    writeFile(fs, "one", "some_content");
    writeFile(fs, "two", "some_content");
    writeFile(fs, "three", "some_other_content");

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

  SECTION("mkdirs") {
    SECTION("single directory") {
      mkdirs(fs, abc);
      CHECK(S_ISDIR(fs.stat(abc).metadata.mode));
    }

    SECTION("already existing directory") {
      mkdirs(fs, abc);
      mkdirs(fs, abc);  // Should be ok
      CHECK(S_ISDIR(fs.stat(abc).metadata.mode));
    }

    SECTION("over file") {
      fs.open(abc, "w");
      CHECK_THROWS_AS(mkdirs(fs, abc), IoError);
    }

    SECTION("several directories") {
      const std::string dir_path = "abc/def/ghi";
      const std::string file_path = "abc/def/ghi/jkl";
      mkdirs(fs, dir_path);
      writeFile(fs, file_path, "hello");
    }
  }

  SECTION("mkdirsFor") {
    const std::string file_path = "abc/def/ghi/jkl";
    mkdirsFor(fs, file_path);
    fs.open(file_path, "w");
    CHECK(fs.stat(file_path).result == 0);
  }
}

}  // namespace shk
