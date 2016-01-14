#include <catch.hpp>
#include <rapidcheck/catch.h>

#include "in_memory_file_system.h"
#include "generators.h"

namespace shk {

TEST_CASE("detail::basenameSplit") {
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

TEST_CASE("InMemoryFileSystem::lstat missing file") {
  Paths paths;
  InMemoryFileSystem fs(paths);
  const auto stat = fs.lstat(paths.get("abc"));
  CHECK(stat.result == ENOENT);
}

TEST_CASE("InMemoryFileSystem::stat missing file") {
  Paths paths;
  InMemoryFileSystem fs(paths);
  const auto stat = fs.stat(paths.get("abc"));
  CHECK(stat.result == ENOENT);
}

TEST_CASE("InMemoryFileSystem::mkdir") {
  Paths paths;
  InMemoryFileSystem fs(paths);
  const auto path = paths.get("abc");
  fs.mkdir(path);

  const auto stat = fs.stat(path);
  CHECK(stat.result == 0);
}

TEST_CASE("InMemoryFileSystem::mkdir over existing directory") {
  Paths paths;
  InMemoryFileSystem fs(paths);
  const auto path = paths.get("abc");
  fs.mkdir(path);
  CHECK_THROWS_AS(fs.mkdir(path), IoError);
}

TEST_CASE("InMemoryFileSystem::rmdir missing file") {
  Paths paths;
  InMemoryFileSystem fs(paths);
  const auto path = paths.get("abc");
  CHECK_THROWS_AS(fs.rmdir(path), IoError);
}

TEST_CASE("InMemoryFileSystem::rmdir") {
  Paths paths;
  InMemoryFileSystem fs(paths);
  const auto path = paths.get("abc");
  fs.mkdir(path);
  fs.rmdir(path);

  CHECK(fs.stat(path).result == ENOENT);
}

TEST_CASE("InMemoryFileSystem::rmdir nonempty directory") {
  Paths paths;
  InMemoryFileSystem fs(paths);
  const auto path = paths.get("abc");
  const auto file_path = paths.get("abc/def");
  fs.mkdir(path);
  fs.open(file_path, "w");
  CHECK_THROWS_AS(fs.rmdir(path), IoError);
  CHECK(fs.stat(path).result == 0);
}

TEST_CASE("InMemoryFileSystem::unlink directory") {
  Paths paths;
  InMemoryFileSystem fs(paths);
  const auto path = paths.get("abc");
  fs.mkdir(path);
  CHECK_THROWS_AS(fs.unlink(path), IoError);
}

TEST_CASE("InMemoryFileSystem::unlink") {
  Paths paths;
  InMemoryFileSystem fs(paths);
  const auto path = paths.get("abc");
  fs.open(path, "w");

  fs.unlink(path);
  CHECK(fs.stat(path).result == ENOENT);
}

TEST_CASE("InMemoryFileSystem::open for writing") {
  Paths paths;
  InMemoryFileSystem fs(paths);
  const auto path = paths.get("abc");
  fs.open(path, "w");

  CHECK(fs.stat(path).result == 0);
}

TEST_CASE("InMemoryFileSystem::open missing file for reading") {
  Paths paths;
  InMemoryFileSystem fs(paths);
  const auto path = paths.get("abc");
  CHECK_THROWS_AS(fs.open(path, "r"), IoError);
}

}  // namespace shk
