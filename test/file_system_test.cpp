#include <catch.hpp>

#include "file_system.h"

#include "in_memory_file_system.h"

namespace shk {

TEST_CASE("FileSystem") {
  InMemoryFileSystem fs;

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

  SECTION("hashDir") {
    fs.mkdir("d");
    fs.mkdir("e");

    CHECK(fs.hashDir("d") == fs.hashDir("e"));

    fs.mkdir("d/d");
    const auto hash_with_one_dir = fs.hashDir("d");
    CHECK(hash_with_one_dir != fs.hashDir("e"));

    fs.open("d/e", "w");
    const auto hash_with_one_dir_and_one_file = fs.hashDir("d");
    CHECK(hash_with_one_dir_and_one_file != hash_with_one_dir);
    CHECK(hash_with_one_dir_and_one_file != fs.hashDir("e"));

    fs.unlink("d/e");
    CHECK(hash_with_one_dir == fs.hashDir("d"));

    fs.rmdir("d/d");
    CHECK(fs.hashDir("d") == fs.hashDir("e"));
  }

  SECTION("writeFile") {
    fs.writeFile("abc", "hello");
    CHECK(fs.stat("abc").result == 0);  // Verify file exists
  }

  SECTION("writeFile, readFile") {
    fs.writeFile("abc", "hello");
    CHECK(fs.readFile("abc") == "hello");
  }

  SECTION("writeFile, writeFile, readFile") {
    fs.writeFile("abc", "hello");
    fs.writeFile("abc", "hello!");
    CHECK(fs.readFile("abc") == "hello!");
  }
}

}  // namespace shk
