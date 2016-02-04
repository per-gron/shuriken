#include <catch.hpp>
#include <rapidcheck/catch.h>

#include "path.h"

#include "generators.h"
#include "in_memory_file_system.h"

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
  StringPiece dn, bn;
  std::tie(dn, bn) = basenameSplitPiece(path);
  CHECK(dn.asString() == dirname);
  CHECK(bn.asString() == basename);
}

class FailingStatFileSystem : public FileSystem {
 public:
  std::unique_ptr<Stream> open(
      const std::string &path, const char *mode) throw(IoError) override {
    return _fs.open(path, mode);
  }
  std::unique_ptr<Mmap> mmap(const std::string &path) throw(IoError) override {
    return _fs.mmap(path);
  }
  Stat stat(const std::string &path) override {
    Stat stat;
    stat.result = ENOENT;
    return stat;
  }
  Stat lstat(const std::string &path) override {
    return _fs.lstat(path);
  }
  void mkdir(const std::string &path) throw(IoError) override {
    _fs.mkdir(path);
  }
  void rmdir(const std::string &path) throw(IoError) override {
    _fs.rmdir(path);
  }
  void unlink(const std::string &path) throw(IoError) override {
    _fs.unlink(path);
  }
  void rename(
      const std::string &old_path,
      const std::string &new_path) throw(IoError) override {
    _fs.rename(old_path, new_path);
  }
  std::vector<DirEntry> readDir(
      const std::string &path) throw(IoError) override {
    return _fs.readDir(path);
  }
  std::string readFile(const std::string &path) throw(IoError) override {
    return _fs.readFile(path);
  }
  Hash hashFile(const std::string &path) throw(IoError) override {
    return _fs.hashFile(path);
  }
  std::string mkstemp(std::string &&filename_template) throw(IoError) override {
    throw _fs.mkstemp(std::move(filename_template));
  }

 private:
  InMemoryFileSystem _fs;
};

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

    rc::prop("extracts the basename and the dirname", []() {
      const auto path_components = *gen::pathComponents();
      RC_PRE(!path_components.empty());

      const auto path_string = gen::joinPathComponents(path_components);
      auto dirname_string = gen::joinPathComponents(
          std::vector<std::string>(
              path_components.begin(),
              path_components.end() - 1));
      if (dirname_string.empty()) {
        dirname_string = ".";
      }

      StringPiece dirname;
      StringPiece basename;
      std::tie(dirname, basename) = basenameSplitPiece(path_string);

      RC_ASSERT(basename == *path_components.rbegin());
      RC_ASSERT(dirname == dirname_string);
    });
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

  SECTION("operator==, operator!=") {
    InMemoryFileSystem fs;
    rc::prop("equal string paths are equal paths", [&fs]() {
      Paths paths(fs);

      const auto path_1_string = *gen::pathString();
      const auto path_2_string = *gen::pathString();

      const auto path_1 = paths.get(path_1_string);
      const auto path_2 = paths.get(path_2_string);

      RC_ASSERT((path_1 == path_2) == (path_1_string == path_2_string));
      RC_ASSERT((path_1 != path_2) == (path_1_string != path_2_string));
    });
  }

  SECTION("Paths.get") {
    InMemoryFileSystem fs;
    fs.open("file", "w");
    fs.open("other_file", "w");
    fs.mkdir("dir");
    Paths paths(fs);

    CHECK(paths.get("/a").isSame(paths.get("a")));
    CHECK(!paths.get("/a").isSame(paths.get("/b")));
    CHECK(!paths.get("/a").isSame(paths.get("/a/b")));
    CHECK(!paths.get("a").isSame(paths.get("b")));
    CHECK(!paths.get("hey").isSame(paths.get("b")));
    CHECK(!paths.get("hey").isSame(paths.get("there")));
    CHECK(!paths.get("a/hey").isSame(paths.get("a/there")));
    CHECK(!paths.get("hey/a").isSame(paths.get("there/a")));

    CHECK_THROWS_AS(paths.get(""), PathError);
    CHECK(paths.get("/") != paths.get("//"));
    CHECK(paths.get("/").isSame(paths.get("//")));
    CHECK(paths.get("/").isSame(paths.get("///")));

    CHECK(paths.get("/missing") != paths.get("//missing"));
    CHECK(paths.get("/missing").isSame(paths.get("//missing")));

    CHECK(paths.get("/file") != paths.get("//file"));
    CHECK(paths.get("/file").isSame(paths.get("//file")));

    CHECK(paths.get("/dir") == paths.get("/dir"));
    CHECK(paths.get("/dir") != paths.get("//dir"));
    CHECK(paths.get("/dir").isSame(paths.get("//dir")));

    CHECK(paths.get("/dir/file").isSame(paths.get("/dir//file")));
    CHECK(paths.get("/./dir/file").isSame(paths.get("/dir//file")));
    CHECK(paths.get("/dir/file").isSame(paths.get("/dir/file/.")));
    CHECK(paths.get("/./dir/file").isSame(paths.get("/dir/./file/../file")));

    CHECK(!paths.get("/dir/file_").isSame(paths.get("/dir/file")));
    CHECK(!paths.get("/file_").isSame(paths.get("/file")));
    CHECK(!paths.get("/dir").isSame(paths.get("/file")));
    CHECK(!paths.get("/other_file").isSame(paths.get("/file")));

    CHECK(paths.get(".").isSame(paths.get("./")));

    CHECK_THROWS_AS(paths.get("/file/"), PathError);
    CHECK_THROWS_AS(paths.get("/file/blah"), PathError);
    CHECK_THROWS_AS(paths.get("/file//"), PathError);
    CHECK_THROWS_AS(paths.get("/file/./"), PathError);
    CHECK_THROWS_AS(paths.get("/file/./x"), PathError);

    FailingStatFileSystem failing_stat_fs;
    CHECK_THROWS_AS(Paths(failing_stat_fs).get("/"), PathError);
    CHECK_THROWS_AS(Paths(failing_stat_fs).get("."), PathError);
  }
}

}  // namespace shk
