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
      fs->writeFile(kTestFilename1, "data");
      CHECK(fs->mmap(kTestFilename1)->memory() == "data");
    }

    SECTION("EmptyFile") {
      fs->writeFile(kTestFilename1, "");
      CHECK(fs->mmap(kTestFilename1)->memory() == "");
    }
  }

  SECTION("mkstemp") {
    SECTION("don't leak file descriptor") {
      const auto before = numOpenFds();
      const auto path = fs->mkstemp("test.XXXXXXXX");
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
    fs->symlink("target", kTestFilename1);
    const auto stat = fs->lstat(kTestFilename1);
    CHECK(stat.result != ENOENT);
    CHECK(S_ISLNK(stat.metadata.mode));
  }

  SECTION("readSymlink") {
    std::string err;
    SECTION("success") {
      fs->symlink("target", kTestFilename1);
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
