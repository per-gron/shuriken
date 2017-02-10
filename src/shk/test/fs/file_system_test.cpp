#include <catch.hpp>

#include <sys/stat.h>

#include "fs/file_system.h"

#include "../in_memory_file_system.h"

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

    CHECK(r_copy == r);
    CHECK(!(r_copy != r));
    CHECK(!(r == d));
    CHECK(r != d);
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

  SECTION("mkdirs") {
    const std::string abc = "abc";

    SECTION("single directory") {
      const auto dirs = mkdirs(fs, abc);
      CHECK(S_ISDIR(fs.stat(abc).metadata.mode));
      CHECK(dirs == std::vector<std::string>({ abc }));
    }

    SECTION("already existing directory") {
      mkdirs(fs, abc);
      const auto dirs = mkdirs(fs, abc);  // Should be ok
      CHECK(S_ISDIR(fs.stat(abc).metadata.mode));
      CHECK(dirs.empty());
    }

    SECTION("over file") {
      fs.open(abc, "w");
      CHECK_THROWS_AS(mkdirs(fs, abc), IoError);
    }

    SECTION("several directories") {
      const std::string dir_path = "abc/def/ghi";
      const std::string file_path = "abc/def/ghi/jkl";
      const auto dirs = mkdirs(fs, dir_path);
      CHECK(dirs == std::vector<std::string>({
          "abc",
          "abc/def",
          "abc/def/ghi" }));
      fs.writeFile(file_path, "hello");
    }
  }

  SECTION("mkdirsFor") {
    const std::string file_path = "abc/def/ghi/jkl";
    mkdirsFor(fs, file_path);
    fs.open(file_path, "w");
    CHECK(fs.stat(file_path).result == 0);
  }
}

}  // namespace shk
