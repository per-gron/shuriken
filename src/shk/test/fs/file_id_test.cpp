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

#include "fs/file_id.h"

namespace shk {

TEST_CASE("FileId") {
  SECTION("create from Stat") {
    Stat stat;
    stat.metadata.ino = 3;
    stat.metadata.dev = 4;
    const FileId id(stat);
    CHECK(id.ino == 3);
    CHECK(id.dev == 4);
  }

  SECTION("operator==") {
    CHECK(FileId(1, 2) == FileId(1, 2));
    CHECK(!(FileId(1, 3) == FileId(1, 2)));
    CHECK(!(FileId(3, 2) == FileId(1, 2)));
  }

  SECTION("operator!=") {
    CHECK(!(FileId(1, 2) != FileId(1, 2)));
    CHECK(FileId(1, 3) != FileId(1, 2));
    CHECK(FileId(3, 2) != FileId(1, 2));
  }

  SECTION("hash") {
    CHECK(std::hash<FileId>()(FileId(1, 2)) == std::hash<FileId>()(FileId(1, 2)));
    CHECK(std::hash<FileId>()(FileId(1, 2)) != std::hash<FileId>()(FileId(2, 2)));
  }
}

}  // namespace shk
