#include <catch.hpp>

#include "persistent_file_system.h"

namespace shk {
namespace {

const char kTestFilename1[] = "filesystem-tempfile1";
const char kTestFilename2[] = "filesystem-tempfile2";

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
      CHECK(fs->mmap(kTestFilename1)->memory().asString() == "data");
    }

    SECTION("EmptyFile") {
      fs->writeFile(kTestFilename1, "");
      CHECK(fs->mmap(kTestFilename1)->memory().asString() == "");
    }
  }

  unlink(kTestFilename1);
  unlink(kTestFilename2);
}

}  // namespace shk
