#include <catch.hpp>

#include <fcntl.h>
#include <unistd.h>

#include "fs/cleaning_file_system.h"

#include "../in_memory_file_system.h"

namespace shk {

TEST_CASE("CleaningFileSystem") {
  const std::string abc = "abc";

  InMemoryFileSystem inner_fs;
  std::string err;
  CHECK(inner_fs.writeFile("f", "contents", &err));
  CHECK(err == "");
  inner_fs.mkdir("dir");

  CleaningFileSystem fs(inner_fs);

  SECTION("mmap") {
    fs.mkdir("dir");
    CHECK_THROWS_AS(fs.mmap("nonexisting"), IoError);
    CHECK_THROWS_AS(fs.mmap("dir"), IoError);
    CHECK_THROWS_AS(fs.mmap("dir/nonexisting"), IoError);
    CHECK_THROWS_AS(fs.mmap("nonexisting/nonexisting"), IoError);
    CHECK(fs.mmap("f")->memory() == "contents");
    CHECK(fs.getRemovedCount() == 0);
  }

  SECTION("open") {
    CHECK(fs.open("f", "r"));
    CHECK(fs.getRemovedCount() == 0);
  }

  SECTION("stat") {
    CHECK(fs.stat("f").result == ENOENT);
    CHECK(fs.lstat("f").result == ENOENT);
    CHECK(fs.stat("nonexisting").result == ENOENT);
    CHECK(fs.lstat("nonexisting").result == ENOENT);
    CHECK(fs.getRemovedCount() == 0);
  }

  SECTION("mkdir") {
    fs.mkdir(abc);
    CHECK(inner_fs.stat(abc).result == ENOENT);
    CHECK(fs.getRemovedCount() == 0);
  }

  SECTION("rmdir") {
    fs.rmdir("dir");
    CHECK(inner_fs.stat("dir").result == ENOENT);
  }

  SECTION("getRemovedCount") {
    CHECK(fs.getRemovedCount() == 0);
    SECTION("rmdir") {
      fs.rmdir("dir");
      CHECK(fs.getRemovedCount() == 1);
    }
    SECTION("unlink") {
      fs.unlink("f");
      CHECK(fs.getRemovedCount() == 1);
    }
    SECTION("both") {
      fs.rmdir("dir");
      fs.unlink("f");
      CHECK(fs.getRemovedCount() == 2);
    }
  }

  SECTION("unlink") {
    fs.unlink("f");
    CHECK(inner_fs.stat("f").result == ENOENT);
  }

  SECTION("symlink") {
    std::string err;
    CHECK(fs.symlink("target", "link", &err));
    CHECK(err == "");
    CHECK(inner_fs.lstat("link").result != ENOENT);
  }

  SECTION("rename") {
    std::string err;
    CHECK(fs.rename("f", "g", &err));
    CHECK(err == "");
    CHECK(inner_fs.stat("f").result == ENOENT);
    CHECK(inner_fs.stat("g").result != ENOENT);
    CHECK(fs.getRemovedCount() == 0);
  }

  SECTION("truncate") {
    std::string err;
    CHECK(fs.truncate("f", 1, &err));
    CHECK(err == "");
    CHECK(
        inner_fs.readFile("f", &err) ==
        std::make_pair(std::string("c"), true));
    CHECK(err == "");
  }

  SECTION("readDir") {
    std::string err_1;
    std::string err_2;
    CHECK(inner_fs.readDir(".", &err_1) == fs.readDir(".", &err_2));
    CHECK(err_1 == "");
    CHECK(err_2 == "");
    CHECK(fs.getRemovedCount() == 0);
  }

  SECTION("readSymlink") {
    std::string err;
    CHECK(inner_fs.symlink("target", "link", &err));
    CHECK(err == "");
    CHECK(
        fs.readSymlink("link", &err) ==
        std::make_pair(std::string("target"), true));
    CHECK(err == "");
  }

  SECTION("readFile") {
    std::string err;
    CHECK(
        fs.readFile("f", &err) ==
        std::make_pair(std::string("contents"), true));
  }

  SECTION("hashFile") {
    std::string err_1;
    std::string err_2;
    CHECK(
        fs.hashFile("f", &err_1) ==
        inner_fs.hashFile("f", &err_2));
    CHECK(err_1 == "");
    CHECK(err_2 == "");
    CHECK(fs.getRemovedCount() == 0);
  }

  SECTION("mkstemp") {
    std::string tmp_file;
    bool success;
    std::string err;
    std::tie(tmp_file, success) = fs.mkstemp("test.XXXXXXXX", &err);
    CHECK(success);
    CHECK(err == "");
    CHECK(tmp_file != "");
    CHECK(inner_fs.stat(tmp_file).result != ENOENT);
    CHECK(fs.getRemovedCount() == 0);
  }
}

}  // namespace shk
