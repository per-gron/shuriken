#include <catch.hpp>
#include <rapidcheck/catch.h>

#include "path.h"

#include "generators.h"

namespace shk {
namespace {

std::string canonicalizePath(std::string path) throw(PathError) {
  SlashBits unused;
  detail::canonicalizePath(&path, &unused);
  return path;
}

#ifdef _WIN32
std::pair<std::string, SlashBits> canonicalizePathWithSlashBits(std::string path) throw(PathError) {
  SlashBits slash_bits;
  detail::canonicalizePath(&path, &slash_bits);
  return std::make_pair(path, slash_bits);
}

std::string canonicalizePathError(std::string path) throw(PathError) {
  try {
    canonicalizePath(path);
    CHECK_THROWS((void)0);
    return "";
  } catch (PathError &error) {
    return error.what();
  }
}
#endif

}  // namespace


TEST_CASE("Path") {
  SECTION("operator==") {
    rc::prop("equal string paths are equal paths", []() {
      Paths paths;

      const auto path_1_string = *gen::pathString();
      const auto path_2_string = *gen::pathString();

      const auto path_1 = paths.get(path_1_string);
      const auto path_2 = paths.get(path_2_string);

      RC_ASSERT((path_1 == path_2) == (path_1_string == path_2_string));
    });
  }

  SECTION("canonicalizePath") {
    SECTION("Path samples") {
      CHECK("" == canonicalizePath(""));
      CHECK("" == canonicalizePath("."));
      CHECK("" == canonicalizePath("./."));
      CHECK("foo.h" == canonicalizePath("foo.h"));
      CHECK("foo.h" == canonicalizePath("./foo.h"));
      CHECK("foo/bar.h" == canonicalizePath("./foo/./bar.h"));
      CHECK("x/bar.h" == canonicalizePath("./x/foo/../bar.h"));
      CHECK("bar.h" == canonicalizePath("./x/foo/../../bar.h"));
      CHECK("foo/bar" == canonicalizePath("foo//bar"));
      CHECK("bar" == canonicalizePath("foo//.//..///bar"));
      CHECK("../bar.h" == canonicalizePath("./x/../foo/../../bar.h"));
      CHECK("foo" == canonicalizePath("foo/./."));
      CHECK("foo" == canonicalizePath("foo/bar/.."));
      CHECK("foo/.hidden_bar" == canonicalizePath("foo/.hidden_bar"));
      CHECK("/foo" == canonicalizePath("/foo"));
#ifdef _WIN32
      CHECK("//foo" == canonicalizePath("//foo"));
#else
      CHECK("/foo" == canonicalizePath("//foo"));
#endif
      CHECK("" == canonicalizePath("/"));
    }

#ifdef _WIN32
    SECTION("Path samples on Windows") {
      CHECK("foo.h" == canonicalizePath(".\\foo.h"));
      CHECK("foo/bar.h" == canonicalizePath(".\\foo\\.\\bar.h"));
      CHECK("x/bar.h" == canonicalizePath(".\\x\\foo\\..\\bar.h"));
      CHECK("bar.h" == canonicalizePath(".\\x\\foo\\..\\..\\bar.h"));
      CHECK("foo/bar" == canonicalizePath("foo\\\\bar"));
      CHECK("bar" == canonicalizePath("foo\\\\.\\\\..\\\\\\bar"));
      CHECK("../bar.h" == canonicalizePath(".\\x\\..\\foo\\..\\..\\bar.h"));
      CHECK("foo" == canonicalizePath("foo\\.\\."));
      CHECK("foo" == canonicalizePath("foo\\bar\\.."));
      CHECK("foo/.hidden_bar" == canonicalizePath("foo\\.hidden_bar"));
      CHECK("/foo" == canonicalizePath("\\foo"));
      CHECK("//foo" == canonicalizePath("\\\\foo"));
      CHECK("" == canonicalizePath("\\"));
    }

    SECTION("Windows Slash Tracking") {
      SECTION("Canonicalize Slash Bits") {
        CHECK(canonicalizePathWithSlashBits("foo.h") == std::make_pair("foo.h", 0));
        CHECK(canonicalizePathWithSlashBits("a\\foo.h") == std::make_pair("a/foo.h", 1));
        CHECK(canonicalizePathWithSlashBits("a/bcd/efh\\foo.h") == std::make_pair("a/bcd/efh/foo.h", 4));
        CHECK(canonicalizePathWithSlashBits("a\\bcd/efh\\foo.h") == std::make_pair("a/bcd/efh/foo.h", 5));
        CHECK(canonicalizePathWithSlashBits("a\\bcd\\efh\\foo.h") == std::make_pair("a/bcd/efh/foo.h", 7));
        CHECK(canonicalizePathWithSlashBits("a/bcd/efh/foo.h") == std::make_pair("a/bcd/efh/foo.h", 0));
        CHECK(canonicalizePathWithSlashBits("a\\./efh\\foo.h") == std::make_pair("a/efh/foo.h", 3));
        CHECK(canonicalizePathWithSlashBits("a\\../efh\\foo.h") == std::make_pair("efh/foo.h", 1));
        CHECK(canonicalizePathWithSlashBits("a\\b\\c\\d\\e\\f\\g\\foo.h") == std::make_pair("a/b/c/d/e/f/g/foo.h", 127));
        CHECK(canonicalizePathWithSlashBits("a\\b\\c\\..\\..\\..\\g\\foo.h") == std::make_pair("g/foo.h", 1));
        CHECK(canonicalizePathWithSlashBits("a\\b/c\\../../..\\g\\foo.h") == std::make_pair("g/foo.h", 1));
        CHECK(canonicalizePathWithSlashBits("a\\b/c\\./../..\\g\\foo.h") == std::make_pair("a/g/foo.h", 3));
        CHECK(canonicalizePathWithSlashBits("a\\b/c\\./../..\\g/foo.h") == std::make_pair("a/g/foo.h", 1));
        CHECK(canonicalizePathWithSlashBits("a\\\\\\foo.h") == std::make_pair("a/foo.h", 1));
        CHECK(canonicalizePathWithSlashBits("a/\\\\foo.h") == std::make_pair("a/foo.h", 0));
        CHECK(canonicalizePathWithSlashBits("a\\//foo.h") == std::make_pair("a/foo.h", 1));
      }

      SECTION("Canonicalize Not Exceeding Len") {
        // Make sure searching \/ doesn't go past supplied len.
        char buf[] = "foo/bar\\baz.h\\";  // Last \ past end.
        SlashBits slash_bits;
        size_t size = 13;
        detail::canonicalizePath(buf, &size, &slash_bits);
        EXPECT_EQ(0, strncmp("foo/bar/baz.h", buf, size));
        EXPECT_EQ(2, slash_bits);  // Not including the trailing one.
      }

      SECTION("TooManyComponents") {
        SlashBits slash_bits;
        // 64 is OK.
        std::string path =
            "a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a"
            "/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./x.h";
        detail::canonicalizePath(&path, &slash_bits);

        // Backslashes version.
        path =
            "a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\."
            "\\a\\.\\a\\.\\a\\.\\a\\.\\"
            "a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\."
            "\\a\\.\\a\\.\\a\\.\\a\\.\\x.h";
        detail::canonicalizePath(&path, &slash_bits);
        EXPECT_EQ(slash_bits, 0xffffffff);

        // 65 is not.
        path =
            "a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/"
            "a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./x.h";
        CHECK(detail::canonicalizePathError(path) == "too many path components");

        // Backslashes version.
        path =
            "a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\."
            "\\a\\.\\a\\.\\a\\.\\a\\.\\"
            "a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\."
            "\\a\\.\\a\\.\\a\\.\\a\\.\\a\\x.h";
        CHECK(detail::canonicalizePathError(path) == "too many path components");
      }
    }
#endif

    SECTION("UpDir") {
      CHECK("../../foo/bar.h" == canonicalizePath("../../foo/bar.h"));
      CHECK("../foo/bar.h" == canonicalizePath("test/../../foo/bar.h"));
    }

    SECTION("AbsolutePath") {
      CHECK("/usr/include/stdio.h" == canonicalizePath("/usr/include/stdio.h"));
    }

    SECTION("NotNullTerminated") {
      std::string path;
      size_t len;
      SlashBits unused;

      path = "foo/. bar/.";
      len = strlen("foo/.");  // Canonicalize only the part before the space.
      detail::canonicalizePath(&path[0], &len, &unused);
      CHECK(strlen("foo") == len);
      CHECK("foo/. bar/." == path);

      path = "foo/../file bar/.";
      len = strlen("foo/../file");
      detail::canonicalizePath(&path[0], &len, &unused);
      CHECK(strlen("file") == len);
      CHECK("file ./file bar/." == path);
    }
  }
}

}  // namespace shk
