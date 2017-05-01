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

void writeFile(FileSystem &fs, nt_string_view path, string_view contents) {
  std::string err;
  CHECK(fs.writeFile(path, contents, &err));
  CHECK(err == "");
}

std::string readFile(FileSystem &fs, nt_string_view path) {
  std::string err;
  std::string data;
  bool success;
  std::tie(data, success) = fs.readFile(path, &err);
  CHECK(success);
  CHECK(err == "");
  return data;
}

void rename(FileSystem &fs, nt_string_view old_path, nt_string_view new_path) {
  std::string err;
  CHECK(fs.rename(old_path, new_path, &err));
  CHECK(err == "");
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
    writeFile(fs, "f", "contents");
    fs.mkdir("dir");
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

  SECTION("symlink") {
    std::string err;

    SECTION("success") {
      CHECK(fs.symlink("target", "link", &err));
      CHECK(err == "");
      const auto stat = fs.lstat("link");
      CHECK(stat.result != ENOENT);
      CHECK(S_ISLNK(stat.metadata.mode));
    }

    SECTION("fail") {
      fs.mkdir("link");
      CHECK(!fs.symlink("target", "link", &err));
      CHECK(err != "");
    }
  }

  SECTION("rename") {
    const auto check_rename_fails = [&fs](
        nt_string_view old_path,
        nt_string_view new_path) {
      std::string err;
      CHECK(!fs.rename(old_path, new_path, &err));
      CHECK(err != "");
    };

    SECTION("missing file") {
      check_rename_fails("a", "b");
      check_rename_fails("a/b", "b");
      check_rename_fails("a", "b/a");
    }

    SECTION("directory") {
      fs.mkdir("a");
      fs.open("a/file", "w");
      rename(fs, "a", "b");
      CHECK(fs.stat("a").result == ENOENT);
      CHECK(fs.stat("b").result == 0);
      CHECK(readFile(fs, "b/file") == "");
    }

    SECTION("directory with same name") {
      fs.mkdir("a");
      rename(fs, "a", "a");
      CHECK(fs.stat("a").result == 0);
    }

    SECTION("file") {
      fs.open("a", "w");
      rename(fs, "a", "b");
      CHECK(fs.stat("a").result == ENOENT);
      CHECK(readFile(fs, "b") == "");
    }

    SECTION("update directory mtime") {
      fs.mkdir("a");
      fs.mkdir("b");
      fs.open("a/a", "w");
      now = 123;
      rename(fs, "a/a", "b/b");
      CHECK(fs.stat("a").timestamps.mtime == 123);
      CHECK(fs.stat("a").timestamps.ctime == 123);
      CHECK(fs.stat("b").timestamps.mtime == 123);
      CHECK(fs.stat("b").timestamps.ctime == 123);
    }

    SECTION("file with same name") {
      fs.open("a", "w");
      rename(fs, "a", "a");
      CHECK(fs.stat("a").result == 0);
      CHECK(readFile(fs, "a") == "");
    }

    SECTION("overwrite file with file") {
      writeFile(fs, "a", "a!");
      writeFile(fs, "b", "b!");
      rename(fs, "a", "b");
      CHECK(fs.stat("a").result == ENOENT);
      CHECK(readFile(fs, "b") == "a!");
    }

    SECTION("overwrite directory with file") {
      fs.open("a", "w");
      fs.mkdir("b");
      check_rename_fails("a", "b");
    }

    SECTION("overwrite file with directory") {
      fs.mkdir("a");
      fs.open("b", "w");
      check_rename_fails("a", "b");
    }

    SECTION("overwrite directory with directory") {
      fs.mkdir("a");
      fs.open("a/b", "w");
      fs.mkdir("b");
      rename(fs, "a", "b");
      CHECK(fs.stat("a/b").result == ENOTDIR);
      CHECK(fs.stat("a").result == ENOENT);
      CHECK(fs.stat("b").result == 0);
      CHECK(fs.stat("b/b").result == 0);
    }

    SECTION("overwrite directory with nonempty directory") {
      fs.mkdir("a");
      fs.mkdir("b");
      fs.open("b/b", "w");
      check_rename_fails("a", "b");
    }
  }

  SECTION("truncate") {
    fs.mkdir("dir");
    writeFile(fs, "file", "sweet bananas!");

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
      fs.mkdir("d");
      fs.open("d/a", "w");
      fs.mkdir("d/b");

      std::string err;
      std::vector<DirEntry> dir_entries;
      bool success;
      std::tie(dir_entries, success) = fs.readDir("d", &err);
      CHECK(success);
      CHECK(err == "");
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

      const auto check_readdir_fails = [&fs](nt_string_view path) {
        std::string err;
        std::vector<DirEntry> dir_entries;
        bool success;
        std::tie(dir_entries, success) = fs.readDir(path, &err);
        CHECK(!success);
        CHECK(err != "");
      };

      check_readdir_fails("f");
      check_readdir_fails("f/x");
      check_readdir_fails("nonexisting");
      check_readdir_fails("d/nonexisting");
    }
  }

  SECTION("readSymlink") {
    std::string err;
    SECTION("success") {
      CHECK(fs.symlink("target", "link", &err));
      CHECK(err == "");
      CHECK(
          fs.readSymlink("link", &err) ==
          std::make_pair(std::string("target"), true));
      CHECK(err == "");
    }

    SECTION("fail") {
      CHECK(fs.readSymlink("nonexisting_file", &err).second == false);
      CHECK(err != "");
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
    writeFile(fs, abc, "swe");
    {
      const auto stream = fs.open(abc, "ab");
      const std::string et = "et";
      stream->write(reinterpret_cast<const uint8_t *>(et.data()), et.size(), 1);
    }

    CHECK(readFile(fs, abc) == "sweet");
  }

  SECTION("open new file for appending") {
    {
      const auto stream = fs.open(abc, "ab");
      const std::string et = "et";
      stream->write(reinterpret_cast<const uint8_t *>(et.data()), et.size(), 1);
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
    fs.mkdir("3");
    fs.mkdir("4");

    std::unordered_set<ino_t> inos;
    inos.insert(fs.stat("1").metadata.ino);
    inos.insert(fs.stat("2").metadata.ino);
    inos.insert(fs.stat("3").metadata.ino);
    inos.insert(fs.stat("4").metadata.ino);
    CHECK(inos.size() == 4);
  }

  SECTION("hashFile") {
    writeFile(fs, "one", "some_content");
    writeFile(fs, "two", "some_content");
    writeFile(fs, "three", "some_other_content");

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
