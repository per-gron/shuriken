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

#include <sys/stat.h>

#include <catch.hpp>

#include "in_memory_file_system.h"

namespace shk {
namespace {

std::string testMkstemp(
    FileSystem &file_system, std::string &&filename_template) {
  std::string err;
  std::string path;
  bool success;
  std::tie(path, success) =
      file_system.mkstemp(std::move(filename_template), &err);
  CHECK(success);
  CHECK(err == "");
  return path;
}

std::string readFile(FileSystem &fs, nt_string_view path) {
  std::string data;
  IoError error;
  std::tie(data, error) = fs.readFile(path);
  CHECK(!error);
  return data;
}

}  // anonymous namespace

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
    CHECK(fs.writeFile("f", "contents") == IoError::success());
    CHECK(fs.mkdir("dir") == IoError::success());
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
    CHECK(stream->write(nullptr, 1, 0) == IoError::success());
    CHECK(fs.stat("f").timestamps.mtime == 1235);
    CHECK(fs.stat("f").timestamps.ctime == 1235);
  }

  SECTION("directory mtime/ctime") {
    now = 123;
    CHECK(fs.mkdir("d") == IoError::success());
    CHECK(fs.mkdir("d/subdir") == IoError::success());
    CHECK(fs.stat("d").timestamps.mtime == 123);
    CHECK(fs.stat("d").timestamps.ctime == 123);

    now++;
    fs.open("d/f.txt", "w");
    CHECK(fs.stat("d").timestamps.mtime == 124);
    CHECK(fs.stat("d").timestamps.ctime == 124);

    now++;
    CHECK(fs.unlink("d/f.txt") == IoError::success());
    CHECK(fs.stat("d").timestamps.mtime == 125);
    CHECK(fs.stat("d").timestamps.ctime == 125);

    now++;
    CHECK(fs.rmdir("d/subdir") == IoError::success());
    CHECK(fs.stat("d").timestamps.mtime == 126);
    CHECK(fs.stat("d").timestamps.ctime == 126);
  }

  SECTION("mkdir") {
    CHECK(fs.mkdir(abc) == IoError::success());

    const auto stat = fs.stat(abc);
    CHECK(stat.result == 0);
    CHECK(S_ISDIR(stat.metadata.mode));
  }

  SECTION("mkdir over existing directory") {
    CHECK(fs.mkdir(abc) == IoError::success());
    CHECK(fs.mkdir(abc) != IoError::success());
  }

  SECTION("rmdir missing file") {
    CHECK(fs.rmdir(abc) != IoError::success());
  }

  SECTION("rmdir") {
    CHECK(fs.mkdir(abc) == IoError::success());
    CHECK(fs.rmdir(abc) == IoError::success());

    CHECK(fs.stat(abc).result == ENOENT);
  }

  SECTION("rmdir nonempty directory") {
    const std::string path = "abc";
    const std::string file_path = "abc/def";
    CHECK(fs.mkdir(path) == IoError::success());
    fs.open(file_path, "w");
    CHECK(fs.rmdir(path) != IoError::success());
    CHECK(fs.stat(path).result == 0);
  }

  SECTION("unlink directory") {
    CHECK(fs.mkdir(abc) == IoError::success());
    CHECK(fs.unlink(abc) != IoError::success());
  }

  SECTION("unlink") {
    fs.open(abc, "w");

    CHECK(fs.unlink(abc) == IoError::success());
    CHECK(fs.stat(abc).result == ENOENT);
  }

  SECTION("symlink") {
    SECTION("success") {
      CHECK(fs.symlink("target", "link") == IoError::success());
      const auto stat = fs.lstat("link");
      CHECK(stat.result != ENOENT);
      CHECK(S_ISLNK(stat.metadata.mode));
    }

    SECTION("fail") {
      CHECK(fs.mkdir("link") == IoError::success());
      CHECK(fs.symlink("target", "link") != IoError::success());
    }
  }

  SECTION("rename") {
    SECTION("missing file") {
      CHECK(fs.rename("a", "b") != IoError::success());
      CHECK(fs.rename("a/b", "b") != IoError::success());
      CHECK(fs.rename("a", "b/a") != IoError::success());
    }

    SECTION("directory") {
      CHECK(fs.mkdir("a") == IoError::success());
      fs.open("a/file", "w");
      CHECK(fs.rename("a", "b") == IoError::success());
      CHECK(fs.stat("a").result == ENOENT);
      CHECK(fs.stat("b").result == 0);
      CHECK(readFile(fs, "b/file") == "");
    }

    SECTION("directory with same name") {
      CHECK(fs.mkdir("a") == IoError::success());
      CHECK(fs.rename("a", "a") == IoError::success());
      CHECK(fs.stat("a").result == 0);
    }

    SECTION("file") {
      fs.open("a", "w");
      CHECK(fs.rename("a", "b") == IoError::success());
      CHECK(fs.stat("a").result == ENOENT);
      CHECK(readFile(fs, "b") == "");
    }

    SECTION("update directory mtime") {
      CHECK(fs.mkdir("a") == IoError::success());
      CHECK(fs.mkdir("b") == IoError::success());
      fs.open("a/a", "w");
      now = 123;
      CHECK(fs.rename("a/a", "b/b") == IoError::success());
      CHECK(fs.stat("a").timestamps.mtime == 123);
      CHECK(fs.stat("a").timestamps.ctime == 123);
      CHECK(fs.stat("b").timestamps.mtime == 123);
      CHECK(fs.stat("b").timestamps.ctime == 123);
    }

    SECTION("file with same name") {
      fs.open("a", "w");
      CHECK(fs.rename("a", "a") == IoError::success());
      CHECK(fs.stat("a").result == 0);
      CHECK(readFile(fs, "a") == "");
    }

    SECTION("overwrite file with file") {
      CHECK(fs.writeFile("a", "a!") == IoError::success());
      CHECK(fs.writeFile("b", "b!") == IoError::success());
      CHECK(fs.rename("a", "b") == IoError::success());
      CHECK(fs.stat("a").result == ENOENT);
      CHECK(readFile(fs, "b") == "a!");
    }

    SECTION("overwrite directory with file") {
      fs.open("a", "w");
      CHECK(fs.mkdir("b") == IoError::success());
      CHECK(fs.rename("a", "b") != IoError::success());
    }

    SECTION("overwrite file with directory") {
      CHECK(fs.mkdir("a") == IoError::success());
      fs.open("b", "w");
      CHECK(fs.rename("a", "b") != IoError::success());
    }

    SECTION("overwrite directory with directory") {
      CHECK(fs.mkdir("a") == IoError::success());
      fs.open("a/b", "w");
      CHECK(fs.mkdir("b") == IoError::success());
      CHECK(fs.rename("a", "b") == IoError::success());
      CHECK(fs.stat("a/b").result == ENOTDIR);
      CHECK(fs.stat("a").result == ENOENT);
      CHECK(fs.stat("b").result == 0);
      CHECK(fs.stat("b/b").result == 0);
    }

    SECTION("overwrite directory with nonempty directory") {
      CHECK(fs.mkdir("a") == IoError::success());
      CHECK(fs.mkdir("b") == IoError::success());
      fs.open("b/b", "w");
      CHECK(fs.rename("a", "b") != IoError::success());
    }
  }

  SECTION("truncate") {
    CHECK(fs.mkdir("dir") == IoError::success());
    CHECK(fs.writeFile("file", "sweet bananas!") == IoError::success());

    const auto check_truncate_fails = [&fs](nt_string_view path, size_t size) {
      CHECK(fs.truncate(path, size) != IoError::success());
    };

    check_truncate_fails("dir", 0);
    check_truncate_fails("missing", 0);
    check_truncate_fails("dir/missing", 0);
    check_truncate_fails("missing/a", 0);

    std::string err;
    CHECK(fs.truncate("file", 5) == IoError::success());
    CHECK(err == "");
    CHECK(readFile(fs, "file") == "sweet");
  }

  SECTION("readDir") {
    SECTION("success") {
      CHECK(fs.mkdir("d") == IoError::success());
      fs.open("d/a", "w");
      CHECK(fs.mkdir("d/b") == IoError::success());

      std::vector<DirEntry> dir_entries;
      IoError error;
      std::tie(dir_entries, error) = fs.readDir("d");
      CHECK(!error);
      std::sort(dir_entries.begin(), dir_entries.end());
      REQUIRE(dir_entries.size() == 2);
      CHECK(dir_entries[0].type == DirEntry::Type::REG);
      CHECK(dir_entries[0].name == "a");
      CHECK(dir_entries[1].type == DirEntry::Type::DIR);
      CHECK(dir_entries[1].name == "b");
    }

    SECTION("fail") {
      fs.open("f", "w");
      CHECK(fs.mkdir("d") == IoError::success());

      const auto check_readdir_fails = [&fs](nt_string_view path) {
        std::vector<DirEntry> dir_entries;
        IoError error;
        std::tie(dir_entries, error) = fs.readDir(path);
        CHECK(error);
      };

      check_readdir_fails("f");
      check_readdir_fails("f/x");
      check_readdir_fails("nonexisting");
      check_readdir_fails("d/nonexisting");
    }
  }

  SECTION("readSymlink") {
    SECTION("success") {
      CHECK(fs.symlink("target", "link") == IoError::success());
      CHECK(
          fs.readSymlink("link") ==
          std::make_pair(std::string("target"), IoError::success()));
    }

    SECTION("fail") {
      CHECK(fs.readSymlink("nonexisting_file").second != IoError::success());
    }
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
    CHECK(fs.writeFile(abc, "swe") == IoError::success());
    {
      const auto stream = fs.open(abc, "ab");
      const std::string et = "et";
      CHECK(
          stream->write(
              reinterpret_cast<const uint8_t *>(et.data()), et.size(), 1) ==
          IoError::success());
    }

    CHECK(readFile(fs, abc) == "sweet");
  }

  SECTION("open new file for appending") {
    {
      const auto stream = fs.open(abc, "ab");
      const std::string et = "et";
      CHECK(
          stream->write(
              reinterpret_cast<const uint8_t *>(et.data()), et.size(), 1) ==
          IoError::success());
    }

    CHECK(readFile(fs, abc) == "et");
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
    CHECK(fs.mkdir("3") == IoError::success());
    CHECK(fs.mkdir("4") == IoError::success());

    std::unordered_set<ino_t> inos;
    inos.insert(fs.stat("1").metadata.ino);
    inos.insert(fs.stat("2").metadata.ino);
    inos.insert(fs.stat("3").metadata.ino);
    inos.insert(fs.stat("4").metadata.ino);
    CHECK(inos.size() == 4);
  }

  SECTION("hashFile") {
    CHECK(fs.writeFile("one", "some_content") == IoError::success());
    CHECK(fs.writeFile("two", "some_content") == IoError::success());
    CHECK(fs.writeFile("three", "some_other_content") == IoError::success());

    std::string err_1;
    std::string err_2;
    CHECK(fs.hashFile("one", &err_1) == fs.hashFile("one", &err_2));
    CHECK(err_1 == "");
    CHECK(err_2 == "");
    CHECK(fs.hashFile("one", &err_1) == fs.hashFile("two", &err_2));
    CHECK(err_1 == "");
    CHECK(err_2 == "");
    CHECK(fs.hashFile("one", &err_1) != fs.hashFile("three", &err_2));
    CHECK(err_1 == "");
    CHECK(err_2 == "");
  }

  SECTION("mkstemp creates file") {
    const auto path = testMkstemp(fs, "hi.XXX");
    CHECK(fs.stat(path).result == 0);
  }

  SECTION("mkstemp creates unique paths") {
    const auto path1 = testMkstemp(fs, "hi.XXX");
    const auto path2 = testMkstemp(fs, "hi.XXX");
    CHECK(path1 != path2);
    CHECK(fs.stat(path1).result == 0);
    CHECK(fs.stat(path2).result == 0);
  }

  SECTION("enqueueMkstempResult") {
    SECTION("one path") {
      fs.enqueueMkstempResult("one");
      CHECK(testMkstemp(fs, "hi.XXX") == "one");
      CHECK(fs.stat("one").result == ENOENT);
    }

    SECTION("two paths") {
      fs.enqueueMkstempResult("one");
      fs.enqueueMkstempResult("two");
      CHECK(testMkstemp(fs, "hi.XXX") == "one");
      CHECK(fs.stat("one").result == ENOENT);
      CHECK(testMkstemp(fs, "hi.XXX") == "two");
      CHECK(fs.stat("two").result == ENOENT);
    }
  }
}

}  // namespace shk
