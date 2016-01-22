#include <sys/stat.h>

#include <catch.hpp>

#include "in_memory_file_system.h"
#include "generators.h"

namespace shk {

TEST_CASE("InMemoryFileSystem") {
  Paths paths;
  InMemoryFileSystem fs(paths);

  SECTION("lstat missing file") {
    const auto stat = fs.lstat(paths.get("abc"));
    CHECK(stat.result == ENOENT);
  }

  SECTION("stat missing file") {
    const auto stat = fs.stat(paths.get("abc"));
    CHECK(stat.result == ENOENT);
  }

  SECTION("mkdir") {
    const auto path = paths.get("abc");
    fs.mkdir(path);

    const auto stat = fs.stat(path);
    CHECK(stat.result == 0);
    CHECK(S_ISDIR(stat.metadata.mode));
  }

  SECTION("mkdir over existing directory") {
    const auto path = paths.get("abc");
    fs.mkdir(path);
    CHECK_THROWS_AS(fs.mkdir(path), IoError);
  }

  SECTION("rmdir missing file") {
    const auto path = paths.get("abc");
    CHECK_THROWS_AS(fs.rmdir(path), IoError);
  }

  SECTION("rmdir") {
    const auto path = paths.get("abc");
    fs.mkdir(path);
    fs.rmdir(path);

    CHECK(fs.stat(path).result == ENOENT);
  }

  SECTION("rmdir nonempty directory") {
    const auto path = paths.get("abc");
    const auto file_path = paths.get("abc/def");
    fs.mkdir(path);
    fs.open(file_path, "w");
    CHECK_THROWS_AS(fs.rmdir(path), IoError);
    CHECK(fs.stat(path).result == 0);
  }

  SECTION("unlink directory") {
    const auto path = paths.get("abc");
    fs.mkdir(path);
    CHECK_THROWS_AS(fs.unlink(path), IoError);
  }

  SECTION("unlink") {
    const auto path = paths.get("abc");
    fs.open(path, "w");

    fs.unlink(path);
    CHECK(fs.stat(path).result == ENOENT);
  }

  SECTION("open for writing") {
    const auto path = paths.get("abc");
    fs.open(path, "w");

    const auto stat = fs.stat(path);
    CHECK(stat.result == 0);
    CHECK(S_ISREG(stat.metadata.mode));
  }

  SECTION("open missing file for reading") {
    const auto path = paths.get("abc");
    CHECK_THROWS_AS(fs.open(path, "r"), IoError);
  }

  SECTION("open missing file for reading") {
    const auto path = paths.get("abc");
    CHECK_THROWS_AS(fs.open(path, "r"), IoError);
  }

  SECTION("writeFile") {
    const auto path = paths.get("abc");
    writeFile(fs, path, "hello");
    CHECK(fs.stat(path).result == 0);  // Verify file exists
  }

  SECTION("writeFile, readFile") {
    const auto path = paths.get("abc");
    writeFile(fs, path, "hello");
    CHECK(fs.readFile(path) == "hello");
  }

  SECTION("writeFile, writeFile, readFile") {
    const auto path = paths.get("abc");
    writeFile(fs, path, "hello");
    writeFile(fs, path, "hello!");
    CHECK(fs.readFile(path) == "hello!");
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
      const auto dir_path = paths.get("abc");
      mkdirs(fs, dir_path);
      CHECK(S_ISDIR(fs.stat(dir_path).metadata.mode));
    }

    SECTION("already existing directory") {
      const auto dir_path = paths.get("abc");
      mkdirs(fs, dir_path);
      mkdirs(fs, dir_path);  // Should be ok
      CHECK(S_ISDIR(fs.stat(dir_path).metadata.mode));
    }

    SECTION("over file") {
      const auto dir_path = paths.get("abc");
      fs.open(dir_path, "w");
      CHECK_THROWS_AS(mkdirs(fs, dir_path), IoError);
    }

    SECTION("several directories") {
      const auto dir_path = paths.get("abc/def/ghi");
      const auto file_path = paths.get("abc/def/ghi/jkl");
      mkdirs(fs, dir_path);
      writeFile(fs, file_path, "hello");
    }
  }

  SECTION("mkdirsFor") {
    const auto file_path = paths.get("abc/def/ghi/jkl");
    mkdirsFor(fs, file_path);
    fs.open(file_path, "w");
    CHECK(fs.stat(file_path).result == 0);
  }
}

}  // namespace shk
