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
    const auto hash_dir = [&](nt_string_view path, string_view extra_data) {
      const auto result = fs.hashDir(path, extra_data);
      CHECK(result.second == IoError::success());
      return result.first;
    };

    SECTION("directory contents") {
      CHECK(fs.mkdir("d") == IoError::success());
      CHECK(fs.mkdir("e") == IoError::success());

      const auto e = hash_dir("e", "");

      {
        const auto d = hash_dir("d", "");
        CHECK(d == e);
      }

      CHECK(fs.mkdir("d/d") == IoError::success());
      const auto hash_with_one_dir = hash_dir("d", "");
      CHECK(hash_with_one_dir != e);

      CHECK(fs.open("d/e", "w").second == IoError::success());
      {
        const auto hash_with_one_dir_and_one_file = hash_dir("d", "");
        CHECK(hash_with_one_dir_and_one_file != hash_with_one_dir);
        CHECK(hash_with_one_dir_and_one_file != e);
      }

      CHECK(fs.unlink("d/e") == IoError::success());
      CHECK(hash_with_one_dir == hash_dir("d", ""));

      CHECK(fs.rmdir("d/d") == IoError::success());
      CHECK(hash_dir("d", "") == e);
    }

    SECTION("missing directory") {
      CHECK(fs.hashDir("nonexisting", "").second != IoError::success());
    }

    SECTION("extra_data") {
      CHECK(fs.mkdir("d") == IoError::success());
      CHECK(fs.mkdir("e") == IoError::success());

      CHECK(hash_dir("d", "") == hash_dir("e", ""));
      CHECK(hash_dir("d", "a") == hash_dir("e", "a"));
      CHECK(hash_dir("d", "a") != hash_dir("e", ""));
      CHECK(hash_dir("d", "a") != hash_dir("e", "b"));

      CHECK(fs.open("d/e", "w").second == IoError::success());
      CHECK(hash_dir("d", "hey") != hash_dir("e", "hey"));
    }
  }

  SECTION("hashSymlink") {
    const auto hash_symlink = [&](nt_string_view path, string_view extra_data) {
      const auto result = fs.hashSymlink(path, extra_data);
      CHECK(result.second == IoError::success());
      return result.first;
    };

    CHECK(fs.symlink("target", "link_1") == IoError::success());
    CHECK(fs.symlink("target", "link_2") == IoError::success());
    CHECK(fs.symlink("target_other", "link_3") == IoError::success());

    SECTION("contents") {
      const auto link_1 = hash_symlink("link_1", "");
      const auto link_2 = hash_symlink("link_2", "");
      const auto link_3 = hash_symlink("link_3", "");

      CHECK(link_1 == link_2);
      CHECK(link_2 != link_3);
    }

    SECTION("missing symlink") {
      CHECK(fs.hashSymlink("missing", "").second != IoError::success());
    }

    SECTION("extra_data") {
      CHECK(hash_symlink("link_1", "") == hash_symlink("link_2", ""));
      CHECK(hash_symlink("link_1", "a") == hash_symlink("link_2", "a"));
      CHECK(hash_symlink("link_1", "a") != hash_symlink("link_2", ""));
      CHECK(hash_symlink("link_1", "a") != hash_symlink("link_2", "b"));
      CHECK(hash_symlink("link_1", "hey") != hash_symlink("link_3", "hey"));
    }
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
      std::vector<std::string> dirs;
      IoError error;
      std::tie(dirs, error) = mkdirs(fs, abc);
      CHECK(!error);
      CHECK(S_ISDIR(fs.stat(abc).metadata.mode));
      CHECK(dirs == std::vector<std::string>({ abc }));
    }

    SECTION("already existing directory") {
      CHECK(mkdirs(fs, abc).second == IoError::success());
      std::vector<std::string> dirs;
      IoError error;
      std::tie(dirs, error) = mkdirs(fs, abc);  // Should be ok
      CHECK(!error);
      CHECK(S_ISDIR(fs.stat(abc).metadata.mode));
      CHECK(dirs.empty());
    }

    SECTION("over file") {
      CHECK(fs.open(abc, "w").second == IoError::success());
      CHECK(mkdirs(fs, abc).second != IoError::success());
    }

    SECTION("several directories") {
      const std::string dir_path = "abc/def/ghi";
      const std::string file_path = "abc/def/ghi/jkl";
      std::vector<std::string> dirs;
      IoError error;
      std::tie(dirs, error) = mkdirs(fs, dir_path);
      CHECK(!error);
      CHECK(dirs == std::vector<std::string>({
          "abc",
          "abc/def",
          "abc/def/ghi" }));
      CHECK(fs.writeFile(file_path, "hello") == IoError::success());
    }
  }
}

}  // namespace shk
