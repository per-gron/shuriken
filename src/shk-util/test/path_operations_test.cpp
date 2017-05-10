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

#include <util/path_operations.h>

namespace shk {
namespace {

std::string canonicalizePath(std::string path) throw(PathError) {
  shk::canonicalizePath(&path);
  return path;
}

#ifdef _WIN32
std::string canonicalizePathError(std::string path) {
  try {
    canonicalizePath(path);
    CHECK_THROWS((void)0);
    return "";
  } catch (PathError &error) {
    return error.what();
  }
}
#endif

void checkBasenameSplit(
    const std::string &path,
    const std::string &dirname,
    const std::string &basename) {
  string_view dn, bn;
  std::tie(dn, bn) = basenameSplitPiece(path);
  CHECK(std::string(dn) == dirname);
  CHECK(std::string(bn) == basename);
}

}  // anonymous namespace

TEST_CASE("Path") {
  SECTION("basenameSplit") {
    checkBasenameSplit("/usr/lib", "/usr", "lib");
    checkBasenameSplit("/usr/", "/", "usr");
    checkBasenameSplit("/usr/////////", "/", "usr");
    checkBasenameSplit("usr", ".", "usr");
    checkBasenameSplit("/", "/", "/");
    checkBasenameSplit("//", "/", "/");
    checkBasenameSplit("/////", "/", "/");
    checkBasenameSplit(".", ".", ".");
    checkBasenameSplit("..", ".", "..");
    checkBasenameSplit("", ".", "");
  }

  SECTION("dirname") {
    // Not thoroughly tested because it's tested as part of basenameSplit
    CHECK(dirname(".") == ".");
    CHECK(dirname("/") == "/");
    CHECK(dirname("hej") == ".");
    CHECK(dirname("hej/a") == "hej");
    CHECK(dirname("/hej/a") == "/hej");
    CHECK(dirname("/hej") == "/");
  }

  SECTION("canonicalizePath") {
    SECTION("Path samples") {
      CHECK("." == canonicalizePath(""));
      CHECK("." == canonicalizePath("."));
      CHECK("." == canonicalizePath("./."));
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
      CHECK("/" == canonicalizePath("/"));
      CHECK("/" == canonicalizePath("//"));
      CHECK("/" == canonicalizePath("/////"));
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
      CHECK(canonicalizePath("foo.h") == "foo.h");
      CHECK(canonicalizePath("a\\foo.h") == "a/foo.h");
      CHECK(canonicalizePath("a/bcd/efh\\foo.h") == "a/bcd/efh/foo.h");
      CHECK(canonicalizePath("a\\bcd/efh\\foo.h") == "a/bcd/efh/foo.h");
      CHECK(canonicalizePath("a\\bcd\\efh\\foo.h") == "a/bcd/efh/foo.h");
      CHECK(canonicalizePath("a/bcd/efh/foo.h") == "a/bcd/efh/foo.h");
      CHECK(canonicalizePath("a\\./efh\\foo.h") == "a/efh/foo.h");
      CHECK(canonicalizePath("a\\../efh\\foo.h") == "efh/foo.h");
      CHECK(canonicalizePath("a\\b\\c\\d\\e\\f\\g\\foo.h") == "a/b/c/d/e/f/g/foo.h");
      CHECK(canonicalizePath("a\\b\\c\\..\\..\\..\\g\\foo.h") == "g/foo.h");
      CHECK(canonicalizePath("a\\b/c\\../../..\\g\\foo.h") == "g/foo.h");
      CHECK(canonicalizePath("a\\b/c\\./../..\\g\\foo.h") == "a/g/foo.h");
      CHECK(canonicalizePath("a\\b/c\\./../..\\g/foo.h") == "a/g/foo.h");
      CHECK(canonicalizePath("a\\\\\\foo.h") == "a/foo.h");
      CHECK(canonicalizePath("a/\\\\foo.h") == "a/foo.h");
      CHECK(canonicalizePath("a\\//foo.h") == "a/foo.h");
    }

    SECTION("Windows Slash Tracking") {
      SECTION("Canonicalize Not Exceeding Len") {
        // Make sure searching \/ doesn't go past supplied len.
        char buf[] = "foo/bar\\baz.h\\";  // Last \ past end.
        size_t size = 13;
        canonicalizePath(buf, &size);
        EXPECT_EQ(0, strncmp("foo/bar/baz.h", buf, size));
      }

      SECTION("TooManyComponents") {
        // 64 is OK.
        std::string path =
            "a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a"
            "/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./x.h";
        canonicalizePath(&path);

        // Backslashes version.
        path =
            "a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\."
            "\\a\\.\\a\\.\\a\\.\\a\\.\\"
            "a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\."
            "\\a\\.\\a\\.\\a\\.\\a\\.\\x.h";
        canonicalizePath(&path);

        // 65 is not.
        path =
            "a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/"
            "a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./x.h";
        CHECK(canonicalizePathError(path) == "too many path components");

        // Backslashes version.
        path =
            "a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\."
            "\\a\\.\\a\\.\\a\\.\\a\\.\\"
            "a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\."
            "\\a\\.\\a\\.\\a\\.\\a\\.\\a\\x.h";
        CHECK(canonicalizePathError(path) == "too many path components");
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

      path = "foo/. bar/.";
      len = strlen("foo/.");  // Canonicalize only the part before the space.
      canonicalizePath(&path[0], &len);
      CHECK(strlen("foo") == len);
      CHECK("foo/. bar/." == path);

      path = "foo/../file bar/.";
      len = strlen("foo/../file");
      canonicalizePath(&path[0], &len);
      CHECK(strlen("file") == len);
      CHECK("file ./file bar/." == path);
    }
  }
}

}  // namespace shk
