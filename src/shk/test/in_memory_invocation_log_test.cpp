#include <catch.hpp>

#include "in_memory_file_system.h"
#include "in_memory_invocation_log.h"
#include "log/invocations.h"

namespace shk {

TEST_CASE("InMemoryInvocationLog") {
  InMemoryFileSystem fs;
  fs.writeFile("test_file", "hello!");

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

  SECTION("Fingerprint") {
    CHECK(
        log.fingerprint("test_file") ==
        takeFingerprint(fs, 0, "test_file"));
  }

  SECTION("Commands") {
    SECTION("Empty") {
      log.ranCommand(hash, {}, {}, {}, {});
      CHECK(log.entries().count(hash) == 1);
      log.cleanedCommand(hash);
      CHECK(log.entries().empty());
    }

    SECTION("Input") {
      fs.writeFile("file", "");
      log.ranCommand(
          hash, {}, {}, { "file" }, { takeFingerprint(fs, 0, "file").first });
      CHECK(log.entries().size() == 1);
      REQUIRE(log.entries().count(hash) == 1);
      const auto &entry = log.entries().begin()->second;
      CHECK(entry.output_files.empty());
      REQUIRE(entry.input_files.size() == 1);
      CHECK(entry.input_files[0].first == "file");
    }

    SECTION("IgnoreDir") {
      fs.mkdir("dir");
      log.ranCommand(
          hash, {}, {}, { "dir" }, { takeFingerprint(fs, 0, "dir").first });
      CHECK(log.entries().size() == 1);
      REQUIRE(log.entries().count(hash) == 1);
      const auto &entry = log.entries().begin()->second;
      CHECK(entry.output_files.empty());
      CHECK(entry.input_files.empty());
    }

    SECTION("OutputDir") {
      fs.mkdir("dir");
      log.ranCommand(
          hash, { "dir" }, { takeFingerprint(fs, 0, "dir").first }, {}, {});
      CHECK(log.entries().size() == 1);
      REQUIRE(log.entries().count(hash) == 1);
      const auto &entry = log.entries().begin()->second;
      CHECK(entry.output_files.empty());
      CHECK(entry.input_files.empty());
      CHECK(log.createdDirectories().count("dir"));
    }

    SECTION("OutputDirAndFile") {
      fs.mkdir("dir");
      log.ranCommand(
          hash,
          { "dir", "file" },
          {
              takeFingerprint(fs, 0, "dir").first,
              takeFingerprint(fs, 0, "file").first },
          {},
          {});
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
      CHECK(log.invocations().created_directories.empty());
      CHECK(log.invocations().entries.empty());
    }

    SECTION("Directories") {
      fs.mkdir("a");
      log.createdDirectory("a");

      std::unordered_map<FileId, std::string> created_directories{
        { FileId(fs.lstat("a")), "a" } };
      CHECK(log.invocations().created_directories == created_directories);
    }

    SECTION("Commands") {
      log.ranCommand(hash, {}, {}, {}, {});
      CHECK(log.invocations().entries.size() == 1);
      CHECK(log.invocations().entries.count(hash) == 1);

      log.cleanedCommand(hash);
      CHECK(log.invocations().entries.empty());
    }
  }
}

}  // namespace shk
