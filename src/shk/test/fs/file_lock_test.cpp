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

#include <unistd.h>

#include "fs/file_lock.h"

namespace shk {
namespace {

const char kTestFilename1[] = "filelock-tempfile1";
const char kTestFilename2[] = "filelock-tempfile2";

}  // anonymous namespace

TEST_CASE("FileLock") {
  // In case a crashing test left a stale file behind.
  unlink(kTestFilename1);
  unlink(kTestFilename2);

  SECTION("Lock") {
    FileLock lock(kTestFilename1);
  }

  SECTION("DeleteFileWhenDone") {
    {
      FileLock lock(kTestFilename1);
    }
    CHECK(unlink(kTestFilename1) == -1);
    CHECK(errno == ENOENT);
  }

  SECTION("LockAfterLock") {
    {
      FileLock lock(kTestFilename1);
    }
    {
      FileLock lock(kTestFilename1);
    }
  }

  SECTION("LockWhileLockIsHeld") {
    FileLock lock(kTestFilename1);
    try {
      FileLock lock(kTestFilename1);
      CHECK(false);  // Should not reach this point
    } catch (const IoError &error) {
      // Success
    }
  }

  unlink(kTestFilename1);
  unlink(kTestFilename2);
}

}  // namespace shk
