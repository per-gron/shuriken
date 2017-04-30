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
#include <sys/stat.h>
#include <unistd.h>

#include "fs/persistent_file_system.h"

namespace shk {
namespace {

const char kTestFilename1[] = "filesystem-tempfile1";
const char kTestFilename2[] = "filesystem-tempfile2";

int numOpenFds() {
  const auto num_handles = getdtablesize();
  int count = 0;
  for (int i = 0; i < num_handles; i++) {
    const auto fd_flags = fcntl(i, F_GETFD);
    if (fd_flags != -1) {
      count++;
    }
  }
  return count;
}

}  // anonymous namespace

TEST_CASE("PersistentFileSystem") {
  // In case a crashing test left a stale file behind.
  unlink(kTestFilename1);
  unlink(kTestFilename2);

  const auto fs = persistentFileSystem();

  SECTION("Mmap") {
    SECTION("MissingFile") {
      CHECK_THROWS_AS(fs->mmap("nonexisting.file"), IoError);
    }

    SECTION("FileWithContents") {
      std::string err;
      CHECK(fs->writeFile(kTestFilename1, "data", &err));
      CHECK(err == "");
      CHECK(fs->mmap(kTestFilename1)->memory() == "data");
    }

    SECTION("EmptyFile") {
      std::string err;
      CHECK(fs->writeFile(kTestFilename1, "", &err));
      CHECK(err == "");
      CHECK(fs->mmap(kTestFilename1)->memory() == "");
    }
  }

  SECTION("mkstemp") {
    SECTION("don't leak file descriptor") {
      const auto before = numOpenFds();
      std::string err;
      std::string path;
      bool success;
      std::tie(path, success) = fs->mkstemp("test.XXXXXXXX", &err);
      CHECK(success);
      CHECK(path != "");
      CHECK(err == "");
      fs->unlink(path);
      const auto after = numOpenFds();
      CHECK(before == after);
    }
  }

  SECTION("stat") {
    SECTION("return value for nonexisting file") {
      const auto stat = fs->stat("this_file_does_not_exist_1243542");
      CHECK(stat.result == ENOENT);
    }
  }

  SECTION("symlink") {
    std::string err;

    SECTION("success") {
      CHECK(fs->symlink("target", kTestFilename1, &err));
      CHECK(err == "");
      const auto stat = fs->lstat(kTestFilename1);
      CHECK(stat.result != ENOENT);
      CHECK(S_ISLNK(stat.metadata.mode));
    }

    SECTION("fail") {
      CHECK(fs->writeFile(kTestFilename1, "", &err));
      CHECK(err == "");

      CHECK(!fs->symlink("target", kTestFilename1, &err));
      CHECK(err != "");
    }
  }

  SECTION("readSymlink") {
    std::string err;
    SECTION("success") {
      CHECK(fs->symlink("target", kTestFilename1, &err));
      CHECK(err == "");
      CHECK(
          fs->readSymlink(kTestFilename1, &err) ==
          std::make_pair(std::string("target"), true));
      CHECK(err == "");
    }

    SECTION("fail") {
      CHECK(fs->readSymlink("nonexisting_file", &err).second == false);
      CHECK(err != "");
    }
  }

  unlink(kTestFilename1);
  unlink(kTestFilename2);
}

}  // namespace shk
