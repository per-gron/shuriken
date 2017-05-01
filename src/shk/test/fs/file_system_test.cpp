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

#include <sys/stat.h>

#include "fs/file_system.h"

#include "../in_memory_file_system.h"

namespace shk {

TEST_CASE("FileSystem") {
  InMemoryFileSystem fs;

  SECTION("DirEntry") {
    DirEntry r(DirEntry::Type::REG, "f");
    CHECK(r.type == DirEntry::Type::REG);
    CHECK(r.name == "f");

    DirEntry d(DirEntry::Type::DIR, "d");
    CHECK(d.type == DirEntry::Type::DIR);
    CHECK(d.name == "d");

    DirEntry r_copy = r;
    CHECK(d < r);
    CHECK(!(r < d));
    CHECK(!(r < r));
    CHECK(!(r < r_copy));
    CHECK(!(d < d));

    CHECK(r_copy == r);
    CHECK(!(r_copy != r));
    CHECK(!(r == d));
    CHECK(r != d);
  }

  SECTION("hashDir") {
    CHECK(fs.mkdir("d") == IoError::success());
    CHECK(fs.mkdir("e") == IoError::success());

    const auto e = fs.hashDir("e");
    CHECK(e.second == IoError::success());

    {
      const auto d = fs.hashDir("d");
      CHECK(d == e);
      CHECK(d.second == IoError::success());
    }

    CHECK(fs.mkdir("d/d") == IoError::success());
    const auto hash_with_one_dir = fs.hashDir("d");
    CHECK(hash_with_one_dir.second == IoError::success());
    CHECK(hash_with_one_dir.first != e.first);

    fs.open("d/e", "w");
    {
      const auto hash_with_one_dir_and_one_file = fs.hashDir("d");
      CHECK(hash_with_one_dir_and_one_file.second == IoError::success());
      CHECK(hash_with_one_dir_and_one_file.first != hash_with_one_dir.first);
      CHECK(hash_with_one_dir_and_one_file.first != e.first);
    }

    CHECK(fs.unlink("d/e") == IoError::success());
    CHECK(hash_with_one_dir == fs.hashDir("d"));

    CHECK(fs.rmdir("d/d") == IoError::success());
    CHECK(fs.hashDir("d") == e);

    CHECK(fs.hashDir("nonexisting").second != IoError::success());
  }

  SECTION("hashSymlink") {
    CHECK(fs.symlink("target", "link_1") == IoError::success());
    CHECK(fs.symlink("target", "link_2") == IoError::success());
    CHECK(fs.symlink("target_other", "link_3") == IoError::success());

    const auto link_1 = fs.hashSymlink("link_1");
    const auto link_2 = fs.hashSymlink("link_2");
    const auto link_3 = fs.hashSymlink("link_3");

    CHECK(link_1.second == IoError::success());
    CHECK(link_2.second == IoError::success());
    CHECK(link_3.second == IoError::success());

    CHECK(link_1 == link_2);
    CHECK(link_2 != link_3);

    CHECK(fs.hashSymlink("missing").second != IoError::success());
  }

  SECTION("writeFile") {
    CHECK(fs.writeFile("abc", "hello") == IoError::success());
    CHECK(fs.stat("abc").result == 0);  // Verify file exists
  }

  SECTION("writeFile, readFile") {
    CHECK(fs.writeFile("abc", "hello") == IoError::success());
    CHECK(
        fs.readFile("abc") ==
        std::make_pair(std::string("hello"), IoError::success()));
  }

  SECTION("writeFile, writeFile, readFile") {
    CHECK(fs.writeFile("abc", "hello") == IoError::success());
    CHECK(fs.writeFile("abc", "hello!") == IoError::success());
    CHECK(
        fs.readFile("abc") ==
        std::make_pair(std::string("hello!"), IoError::success()));
  }

  SECTION("mkdirs") {
    const std::string abc = "abc";

    SECTION("single directory") {
      const auto dirs = mkdirs(fs, abc);
      CHECK(S_ISDIR(fs.stat(abc).metadata.mode));
      CHECK(dirs == std::vector<std::string>({ abc }));
    }

    SECTION("already existing directory") {
      mkdirs(fs, abc);
      const auto dirs = mkdirs(fs, abc);  // Should be ok
      CHECK(S_ISDIR(fs.stat(abc).metadata.mode));
      CHECK(dirs.empty());
    }

    SECTION("over file") {
      fs.open(abc, "w");
      CHECK_THROWS_AS(mkdirs(fs, abc), IoError);
    }

    SECTION("several directories") {
      const std::string dir_path = "abc/def/ghi";
      const std::string file_path = "abc/def/ghi/jkl";
      const auto dirs = mkdirs(fs, dir_path);
      CHECK(dirs == std::vector<std::string>({
          "abc",
          "abc/def",
          "abc/def/ghi" }));
      CHECK(fs.writeFile(file_path, "hello") == IoError::success());
    }
  }
}

}  // namespace shk
