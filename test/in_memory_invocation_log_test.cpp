#include <catch.hpp>

#include "in_memory_file_system.h"
#include "in_memory_invocation_log.h"
#include "invocations.h"
#include "path.h"

namespace shk {

TEST_CASE("InMemoryInvocationLog") {
  InMemoryFileSystem fs;
  Paths paths(fs);
  InMemoryInvocationLog log;

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
    InvocationLog::Entry entry;
    log.ranCommand(hash, entry);
    CHECK(log.entries().count(hash) == 1);
    log.cleanedCommand(hash);
    CHECK(log.entries().empty());
  }

  SECTION("Invocations") {
    SECTION("InitialState") {
      CHECK(log.invocations(paths).created_directories.empty());
      CHECK(log.invocations(paths).entries.empty());
    }

    SECTION("Directories") {
      log.createdDirectory("a");
      CHECK(log.invocations(paths).created_directories ==
          std::unordered_set<Path>({ paths.get("a") }));
    }

    SECTION("Commands") {
      InvocationLog::Entry entry;

      log.ranCommand(hash, entry);
      CHECK(log.invocations(paths).entries.size() == 1);
      CHECK(log.invocations(paths).entries.count(hash) == 1);

      log.cleanedCommand(hash);
      CHECK(log.invocations(paths).entries.empty());
    }
  }
}

}  // namespace shk
