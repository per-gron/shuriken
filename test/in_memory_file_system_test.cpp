#include <catch.hpp>
#include <rapidcheck/catch.h>

#include "in_memory_file_system.h"
#include "generators.h"

namespace shk {

TEST_CASE("InMemoryFileSystem") {

  SECTION("detail::basenameSplit") {
    rc::prop("extracts the basename and the dirname", []() {
      const auto path_components = *gen::pathComponents();
      RC_PRE(!path_components.empty());

      const auto path_string = gen::joinPathComponents(path_components);
      const auto dirname_string = gen::joinPathComponents(
          std::vector<std::string>(
              path_components.begin(),
              path_components.end() - 1));

      std::string dirname;
      std::string basename;
      std::tie(dirname, basename) = detail::basenameSplit(path_string);

      RC_ASSERT(basename == *path_components.rbegin());
      RC_ASSERT(dirname == dirname_string);
    });
  }

  Paths paths;
  InMemoryFileSystem fs(paths);

  SECTION("lstat missing file") {
    const auto stat = fs.lstat(paths.get("abc"));
    CHECK(stat.result == ENOENT);
  }

  SECTION("stat missing file") {
    const auto stat = fs.stat(paths.get("abc"));
    CHECK(stat.result == ENOENT);
  }

  SECTION("mkdir") {
    const auto path = paths.get("abc");
    fs.mkdir(path);

    const auto stat = fs.stat(path);
    CHECK(stat.result == 0);
  }

  SECTION("mkdir over existing directory") {
    const auto path = paths.get("abc");
    fs.mkdir(path);
    CHECK_THROWS_AS(fs.mkdir(path), IoError);
  }

  SECTION("rmdir missing file") {
    const auto path = paths.get("abc");
    CHECK_THROWS_AS(fs.rmdir(path), IoError);
  }

  SECTION("rmdir") {
    const auto path = paths.get("abc");
    fs.mkdir(path);
    fs.rmdir(path);

    CHECK(fs.stat(path).result == ENOENT);
  }

  SECTION("rmdir nonempty directory") {
    const auto path = paths.get("abc");
    const auto file_path = paths.get("abc/def");
    fs.mkdir(path);
    fs.open(file_path, "w");
    CHECK_THROWS_AS(fs.rmdir(path), IoError);
    CHECK(fs.stat(path).result == 0);
  }

  SECTION("unlink directory") {
    const auto path = paths.get("abc");
    fs.mkdir(path);
    CHECK_THROWS_AS(fs.unlink(path), IoError);
  }

  SECTION("unlink") {
    const auto path = paths.get("abc");
    fs.open(path, "w");

    fs.unlink(path);
    CHECK(fs.stat(path).result == ENOENT);
  }

  SECTION("open for writing") {
    const auto path = paths.get("abc");
    fs.open(path, "w");

    CHECK(fs.stat(path).result == 0);
  }

  SECTION("open missing file for reading") {
    const auto path = paths.get("abc");
    CHECK_THROWS_AS(fs.open(path, "r"), IoError);
  }

  SECTION("open missing file for reading") {
    const auto path = paths.get("abc");
    CHECK_THROWS_AS(fs.open(path, "r"), IoError);
  }

  SECTION("writeFile") {
    const auto path = paths.get("abc");
    writeFile(fs, path, "hello");
    CHECK(fs.stat(path).result == 0);  // Verify file exists
  }

  SECTION("writeFile, readFile") {
    const auto path = paths.get("abc");
    writeFile(fs, path, "hello");
    CHECK(readFile(fs, path) == "hello");
  }

  SECTION("writeFile, writeFile, readFile") {
    const auto path = paths.get("abc");
    writeFile(fs, path, "hello");
    writeFile(fs, path, "hello!");
    CHECK(readFile(fs, path) == "hello!");
  }

}

}  // namespace shk
