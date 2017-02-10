#include <catch.hpp>

#include "fs/path.h"
#include "in_memory_file_system.h"
#include "in_memory_invocation_log.h"
#include "log/invocations.h"

namespace shk {

TEST_CASE("InMemoryInvocationLog") {
  InMemoryFileSystem fs;
  Paths paths(fs);
  InMemoryInvocationLog log(fs, [] { return 0; });

  Hash hash;
  std::fill(hash.data.begin(), hash.data.end(), 123);

  SECTION("InitialState") {
    CHECK(log.createdDirectories().empty());
    CHECK(log.entries().empty());
  }

  SECTION("Directories") {
    log.createdDirectory("a");
    CHECK(log.createdDirectories().size() == 1);
    CHECK(log.createdDirectories().count("a") == 1);
    log.createdDirectory("b");
    CHECK(log.createdDirectories().size() == 2);
    log.removedDirectory("a");
    CHECK(log.createdDirectories().count("a") == 0);
    CHECK(log.createdDirectories().size() == 1);
    log.removedDirectory("b");
    CHECK(log.createdDirectories().empty());
  }

  SECTION("Commands") {
    SECTION("Empty") {
      log.ranCommand(hash, {}, {});
      CHECK(log.entries().count(hash) == 1);
      log.cleanedCommand(hash);
      CHECK(log.entries().empty());
    }

    SECTION("Input") {
      fs.mkdir("dir");
      log.ranCommand(hash, {}, { { "dir", DependencyType::ALWAYS } });
      CHECK(log.entries().size() == 1);
      REQUIRE(log.entries().count(hash) == 1);
      const auto &entry = log.entries().begin()->second;
      CHECK(entry.output_files.empty());
      REQUIRE(entry.input_files.size() == 1);
      REQUIRE(entry.input_files[0].first == "dir");
    }

    SECTION("IgnoreDir") {
      fs.mkdir("dir");
      log.ranCommand(
          hash, {}, { { "dir", DependencyType::IGNORE_IF_DIRECTORY } });
      CHECK(log.entries().size() == 1);
      REQUIRE(log.entries().count(hash) == 1);
      const auto &entry = log.entries().begin()->second;
      CHECK(entry.output_files.empty());
      CHECK(entry.input_files.empty());
    }

    SECTION("OutputDir") {
      fs.mkdir("dir");
      log.ranCommand(hash, { "dir" }, {});
      CHECK(log.entries().size() == 1);
      REQUIRE(log.entries().count(hash) == 1);
      const auto &entry = log.entries().begin()->second;
      CHECK(entry.output_files.empty());
      CHECK(entry.input_files.empty());
      CHECK(log.createdDirectories().count("dir"));
    }

    SECTION("OutputDirAndFile") {
      fs.mkdir("dir");
      log.ranCommand(hash, { "dir", "file" }, {});
      CHECK(log.entries().size() == 1);
      REQUIRE(log.entries().count(hash) == 1);
      const auto &entry = log.entries().begin()->second;
      REQUIRE(entry.output_files.size() == 1);
      REQUIRE(entry.output_files[0].first == "file");
      CHECK(entry.input_files.empty());
      CHECK(log.createdDirectories().count("dir"));
    }
  }

  SECTION("Invocations") {
    SECTION("InitialState") {
      CHECK(log.invocations(paths).created_directories.empty());
      CHECK(log.invocations(paths).entries.empty());
    }

    SECTION("Directories") {
      fs.mkdir("a");
      log.createdDirectory("a");

      const auto path = paths.get("a");
      std::unordered_map<FileId, Path> created_directories;
      REQUIRE(path.fileId());
      created_directories.emplace(*path.fileId(), path);
      CHECK(log.invocations(paths).created_directories == created_directories);
    }

    SECTION("Commands") {
      log.ranCommand(hash, {}, {});
      CHECK(log.invocations(paths).entries.size() == 1);
      CHECK(log.invocations(paths).entries.count(hash) == 1);

      log.cleanedCommand(hash);
      CHECK(log.invocations(paths).entries.empty());
    }
  }
}

}  // namespace shk
