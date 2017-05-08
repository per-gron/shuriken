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

#include "fs/path.h"

#include "../in_memory_file_system.h"

namespace shk {
namespace {

class FailingStatFileSystem : public FileSystem {
 public:
  USE_RESULT std::pair<std::unique_ptr<Stream>, IoError> open(
      nt_string_view path, const char *mode) override {
    return _fs.open(path, mode);
  }
  USE_RESULT std::pair<std::unique_ptr<Mmap>, IoError>
  mmap(nt_string_view path) override {
    return _fs.mmap(path);
  }
  Stat stat(nt_string_view path) override {
    Stat stat;
    stat.result = ENOENT;
    return stat;
  }
  Stat lstat(nt_string_view path) override {
    return stat(path);
  }
  USE_RESULT IoError mkdir(nt_string_view path) override {
    return _fs.mkdir(path);
  }
  USE_RESULT IoError rmdir(nt_string_view path) override {
    return _fs.rmdir(path);
  }
  USE_RESULT IoError unlink(nt_string_view path) override {
    return _fs.unlink(path);
  }
  USE_RESULT IoError symlink(
      nt_string_view target,
      nt_string_view source) override {
    return _fs.symlink(target, source);
  }
  USE_RESULT IoError rename(
      nt_string_view old_path,
      nt_string_view new_path) override {
    return _fs.rename(old_path, new_path);
  }
  USE_RESULT IoError truncate(nt_string_view path, size_t size) override {
    return _fs.truncate(path, size);
  }
  USE_RESULT std::pair<std::vector<DirEntry>, IoError> readDir(
      nt_string_view path) override {
    return _fs.readDir(path);
  }
  USE_RESULT std::pair<std::string, IoError> readSymlink(
      nt_string_view path) override {
    return _fs.readSymlink(path);
  }
  USE_RESULT std::pair<std::string, IoError> readFile(
      nt_string_view path) override {
    return _fs.readFile(path);
  }
  USE_RESULT std::pair<Hash, IoError> hashFile(
      nt_string_view path) override {
    return _fs.hashFile(path);
  }
  USE_RESULT std::pair<std::string, IoError> mkstemp(
      std::string &&filename_template) override {
    throw _fs.mkstemp(std::move(filename_template));
  }

 private:
  InMemoryFileSystem _fs;
};

}  // anonymous namespace

TEST_CASE("Path") {
  SECTION("operator==, operator!=") {
    InMemoryFileSystem fs;
    CHECK(fs.mkdir("dir") == IoError::success());
    CHECK(fs.writeFile("f", "") == IoError::success());
    CHECK(fs.writeFile("dir/f", "") == IoError::success());

    static const char *kPaths[] = {
        "/",
        "/dir",
        "/f",
        "/dir/f",
        "/dir/../f" };

    for (const char *path1_string : kPaths) {
      for (const char *path2_string : kPaths) {
        Paths paths(fs);
        const auto path1 = paths.get(path1_string);
        const auto path2 = paths.get(path2_string);
        if (path1_string == path2_string) {
          CHECK(path1 == path2);
        } else {
          CHECK(path1 != path2);
        }
      }
    }
  }

  SECTION("original") {
    InMemoryFileSystem fs;
    CHECK(fs.open("file", "w").second == IoError::success());
    CHECK(fs.open("other_file", "w").second == IoError::success());
    CHECK(fs.mkdir("dir") == IoError::success());
    Paths paths(fs);

    CHECK(paths.get("file").original() == "file");
    CHECK(paths.get("dir/.").original() == "dir/.");
    CHECK(paths.get("dir/../nonexisting").original() == "dir/../nonexisting");
  }

  SECTION("exists") {
    InMemoryFileSystem fs;
    CHECK(fs.open("file", "w").second == IoError::success());
    CHECK(fs.open("other_file", "w").second == IoError::success());
    CHECK(fs.mkdir("dir") == IoError::success());
    Paths paths(fs);

    CHECK(paths.get("file").exists());
    CHECK(paths.get("dir/.").exists());
    CHECK(!paths.get("dir/../nonexisting").exists());
    CHECK(!paths.get("nonexisting").exists());
  }

  SECTION("fileId") {
    InMemoryFileSystem fs;
    CHECK(fs.open("file", "w").second == IoError::success());
    CHECK(fs.open("other_file", "w").second == IoError::success());
    CHECK(fs.mkdir("dir") == IoError::success());
    Paths paths(fs);

    const auto file = fs.stat("file").metadata;
    const auto dir = fs.stat("dir").metadata;

    CHECK(paths.get("file").fileId() == FileId(file.ino, file.dev));
    CHECK(paths.get("dir/.").fileId() == FileId(dir.ino, dir.dev));
    CHECK(!paths.get("dir/../nonexisting").fileId());
    CHECK(!paths.get("nonexisting").fileId());
  }

  SECTION("Paths.get") {
    InMemoryFileSystem fs;
    CHECK(fs.open("file", "w").second == IoError::success());
    CHECK(fs.open("other_file", "w").second == IoError::success());
    CHECK(fs.mkdir("dir") == IoError::success());
    Paths paths(fs);

    CHECK(paths.get("/a").isSame(paths.get("a")));
    CHECK(!paths.get("/a").isSame(paths.get("/b")));
    CHECK(!paths.get("/a").isSame(paths.get("/a/b")));
    CHECK(!paths.get("a").isSame(paths.get("b")));
    CHECK(!paths.get("hey").isSame(paths.get("b")));
    CHECK(!paths.get("hey").isSame(paths.get("there")));
    CHECK(!paths.get("a/hey").isSame(paths.get("a/there")));
    CHECK(!paths.get("hey/a").isSame(paths.get("there/a")));

    CHECK_THROWS_AS(paths.get(""), PathError);
    CHECK(paths.get("/") != paths.get("//"));
    CHECK(paths.get("/").isSame(paths.get("//")));
    CHECK(paths.get("/").isSame(paths.get("///")));

    CHECK(paths.get("/missing") != paths.get("//missing"));
    CHECK(paths.get("/missing").isSame(paths.get("//missing")));

    CHECK(paths.get("/file") != paths.get("//file"));
    CHECK(paths.get("/file").isSame(paths.get("//file")));

    CHECK(paths.get("/dir") == paths.get("/dir"));
    CHECK(paths.get("/dir") != paths.get("//dir"));
    CHECK(paths.get("/dir").isSame(paths.get("//dir")));

    CHECK(paths.get("/dir/file").isSame(paths.get("/dir//file")));
    CHECK(paths.get("/./dir/file").isSame(paths.get("/dir//file")));
    CHECK(paths.get("/dir/file").isSame(paths.get("/dir/file/.")));
    CHECK(paths.get("/./dir/file").isSame(paths.get("/dir/./file/../file")));

    CHECK(!paths.get("/dir/file_").isSame(paths.get("/dir/file")));
    CHECK(!paths.get("/file_").isSame(paths.get("/file")));
    CHECK(!paths.get("/dir").isSame(paths.get("/file")));
    CHECK(!paths.get("/other_file").isSame(paths.get("/file")));

    CHECK(paths.get(".").isSame(paths.get("./")));

    CHECK_THROWS_AS(paths.get("/file/"), PathError);
    CHECK_THROWS_AS(paths.get("/file/blah"), PathError);
    CHECK_THROWS_AS(paths.get("/file//"), PathError);
    CHECK_THROWS_AS(paths.get("/file/./"), PathError);
    CHECK_THROWS_AS(paths.get("/file/./x"), PathError);

    FailingStatFileSystem failing_stat_fs;
    CHECK_THROWS_AS(Paths(failing_stat_fs).get("/"), PathError);
    CHECK_THROWS_AS(Paths(failing_stat_fs).get("."), PathError);
  }

  SECTION("IsSameHash") {
    InMemoryFileSystem fs;
    Paths paths(fs);
    std::unordered_set<Path, Path::IsSameHash, Path::IsSame> set;

    const auto a = paths.get("a/./b");
    const auto b = paths.get("a/b");
    set.insert(a);
    CHECK(set.find(b) != set.end());
    set.insert(b);
    CHECK(set.size() == 1);
  }
}

}  // namespace shk
