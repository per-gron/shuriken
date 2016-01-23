#include <catch.hpp>

#include "file_system.h"

namespace shk {

TEST_CASE("FileSystem") {
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
  }
}

}  // namespace shk
