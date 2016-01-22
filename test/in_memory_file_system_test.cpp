#include <sys/stat.h>

#include <catch.hpp>

#include "in_memory_file_system.h"
#include "generators.h"

namespace shk {

TEST_CASE("InMemoryFileSystem") {
  Paths paths;
  InMemoryFileSystem fs(paths);
  const std::string abc = "abc";

  SECTION("lstat missing file") {
    const auto stat = fs.lstat("abc");
    CHECK(stat.result == ENOENT);
  }

  SECTION("stat missing file") {
    const auto stat = fs.stat("abc");
    CHECK(stat.result == ENOENT);
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

  SECTION("open for writing") {
    fs.open(abc, "w");

    const auto stat = fs.stat(abc);
    CHECK(stat.result == 0);
    CHECK(S_ISREG(stat.metadata.mode));
  }

  SECTION("open missing file for reading") {
    CHECK_THROWS_AS(fs.open("abc", "r"), IoError);
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
