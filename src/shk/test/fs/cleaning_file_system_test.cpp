// Copyright 2017 Per Grön. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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
  CHECK(inner_fs.mkdir("dir") == IoError::success());

  CleaningFileSystem fs(inner_fs);

  SECTION("mmap") {
    CHECK(fs.mkdir("dir") == IoError::success());
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
    CHECK(fs.mkdir(abc) == IoError::success());
    CHECK(inner_fs.stat(abc).result == ENOENT);
    CHECK(fs.getRemovedCount() == 0);
  }

  SECTION("rmdir") {
    CHECK(fs.rmdir("dir") == IoError::success());
    CHECK(inner_fs.stat("dir").result == ENOENT);
  }

  SECTION("getRemovedCount") {
    CHECK(fs.getRemovedCount() == 0);
    SECTION("rmdir") {
      CHECK(fs.rmdir("dir") == IoError::success());
      CHECK(fs.getRemovedCount() == 1);
    }
    SECTION("unlink") {
      CHECK(fs.unlink("f") == IoError::success());
      CHECK(fs.getRemovedCount() == 1);
    }
    SECTION("both") {
      CHECK(fs.rmdir("dir") == IoError::success());
      CHECK(fs.unlink("f") == IoError::success());
      CHECK(fs.getRemovedCount() == 2);
    }
  }

  SECTION("unlink") {
    CHECK(fs.unlink("f") == IoError::success());
    CHECK(inner_fs.stat("f").result == ENOENT);
  }

  SECTION("symlink") {
    CHECK(fs.symlink("target", "link") == IoError::success());
    CHECK(inner_fs.lstat("link").result != ENOENT);
  }

  SECTION("rename") {
    CHECK(fs.rename("f", "g") == IoError::success());
    CHECK(inner_fs.stat("f").result == ENOENT);
    CHECK(inner_fs.stat("g").result != ENOENT);
    CHECK(fs.getRemovedCount() == 0);
  }

  SECTION("truncate") {
    std::string err;
    CHECK(fs.truncate("f", 1) == IoError::success());
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
    CHECK(inner_fs.symlink("target", "link") == IoError::success());
    std::string err;
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
