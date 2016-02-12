#include <catch.hpp>

#include "file_id.h"

namespace shk {

TEST_CASE("FileId") {
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
