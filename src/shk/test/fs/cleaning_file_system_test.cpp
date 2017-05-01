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
  CHECK(inner_fs.writeFile("f", "contents") == IoError::success());
  CHECK(inner_fs.mkdir("dir") == IoError::success());

  CleaningFileSystem fs(inner_fs);

  SECTION("mmap") {
    CHECK(fs.mkdir("dir") == IoError::success());
    CHECK(fs.mmap("nonexisting").second != IoError::success());
    CHECK(fs.mmap("dir").second != IoError::success());
    CHECK(fs.mmap("dir/nonexisting").second != IoError::success());
    CHECK(fs.mmap("nonexisting/nonexisting").second != IoError::success());

    std::unique_ptr<FileSystem::Mmap> mmap;
    IoError error;
    std::tie(mmap, error) = fs.mmap("f");
    REQUIRE(!error);
    CHECK(mmap->memory() == "contents");
  }

  SECTION("open") {
    std::unique_ptr<FileSystem::Stream> stream;
    IoError error;
    std::tie(stream, error) = fs.open("f", "r");
    CHECK(!error);
    CHECK(stream);
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
    SECTION("unlink fail") {
      CHECK(fs.unlink("dir") != IoError::success());
      CHECK(fs.getRemovedCount() == 0);
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
    CHECK(fs.truncate("f", 1) == IoError::success());
    CHECK(
        inner_fs.readFile("f") ==
        std::make_pair(std::string("c"), IoError::success()));
  }

  SECTION("readDir") {
    auto inner = inner_fs.readDir(".");
    auto outer = fs.readDir(".");
    CHECK(inner == outer);
    CHECK(inner.second == IoError::success());
    CHECK(outer.second == IoError::success());
    CHECK(fs.getRemovedCount() == 0);
  }

  SECTION("readSymlink") {
    CHECK(inner_fs.symlink("target", "link") == IoError::success());
    CHECK(
        fs.readSymlink("link") ==
        std::make_pair(std::string("target"), IoError::success()));
  }

  SECTION("readFile") {
    CHECK(
        fs.readFile("f") ==
        std::make_pair(std::string("contents"), IoError::success()));
  }

  SECTION("hashFile") {
    auto outer = fs.hashFile("f");
    auto inner = inner_fs.hashFile("f");
    CHECK(outer == inner);
    CHECK(outer.second == IoError::success());
    CHECK(inner.second == IoError::success());
    CHECK(fs.getRemovedCount() == 0);
  }

  SECTION("mkstemp") {
    std::string tmp_file;
    IoError error;
    std::tie(tmp_file, error) = fs.mkstemp("test.XXXXXXXX");
    CHECK(!error);
    CHECK(tmp_file != "");
    CHECK(inner_fs.stat(tmp_file).result != ENOENT);
    CHECK(fs.getRemovedCount() == 0);
  }
}

}  // namespace shk
