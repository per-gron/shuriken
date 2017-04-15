#include <catch.hpp>

#include <fcntl.h>
#include <unistd.h>

#include "fs/dry_run_file_system.h"

#include "../in_memory_file_system.h"

namespace shk {

TEST_CASE("DryRunFileSystem") {
  const std::string abc = "abc";

  InMemoryFileSystem inner_fs;
  inner_fs.writeFile("f", "contents");
  inner_fs.mkdir("dir");

  const auto fs = dryRunFileSystem(inner_fs);

  SECTION("mmap") {
    fs->mkdir("dir");
    CHECK_THROWS_AS(fs->mmap("nonexisting"), IoError);
    CHECK_THROWS_AS(fs->mmap("dir"), IoError);
    CHECK_THROWS_AS(fs->mmap("dir/nonexisting"), IoError);
    CHECK_THROWS_AS(fs->mmap("nonexisting/nonexisting"), IoError);
    CHECK(fs->mmap("f")->memory() == "contents");
  }

  SECTION("open") {
    // Not implemented
    CHECK_THROWS_AS(fs->open("f", "r"), IoError);
  }

  SECTION("stat") {
    CHECK(fs->stat("f").timestamps.mtime == inner_fs.stat("f").timestamps.mtime);
    CHECK(fs->lstat("f").timestamps.mtime == inner_fs.lstat("f").timestamps.mtime);
  }

  SECTION("mkdir") {
    fs->mkdir(abc);
    CHECK(inner_fs.stat(abc).result == ENOENT);
  }

  SECTION("rmdir") {
    fs->rmdir("dir");
    CHECK(inner_fs.stat("dir").result != ENOENT);
  }

  SECTION("unlink") {
    fs->unlink("f");
    CHECK(inner_fs.stat("f").result != ENOENT);
  }

  SECTION("symlink") {
    fs->symlink("target", "link");
    CHECK(inner_fs.stat("link").result == ENOENT);
  }

  SECTION("unlink") {
    fs->rename("f", "g");
    CHECK(inner_fs.stat("f").result != ENOENT);
    CHECK(inner_fs.stat("g").result == ENOENT);
  }

  SECTION("truncate") {
    fs->truncate("f", 1);
    CHECK(inner_fs.readFile("f") == "contents");
  }

  SECTION("readDir") {
    CHECK(inner_fs.readDir(".") == fs->readDir("."));
  }

  SECTION("readSymlink") {
    inner_fs.symlink("target", "link");
    CHECK(fs->readSymlink("link") == "target");
  }

  SECTION("readFile") {
    CHECK(fs->readFile("f") == "contents");
  }

  SECTION("hashFile") {
    CHECK(fs->hashFile("f") == inner_fs.hashFile("f"));
  }

  SECTION("mkstemp") {
    CHECK(fs->mkstemp("test.XXXXXXXX") == "");
  }
}

}  // namespace shk
