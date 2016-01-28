#include <catch.hpp>

#include "in_memory_invocation_log.h"

namespace shk {

TEST_CASE("InMemoryInvocationLog") {
  InMemoryInvocationLog log;

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
    Hash hash;
    std::fill(hash.data.begin(), hash.data.end(), 123);
    InvocationLog::Entry entry;
    log.ranCommand(hash, entry);
    CHECK(log.entries().count(hash) == 1);
    log.cleanedCommand(hash);
    CHECK(log.entries().empty());
  }
}

}  // namespace shk
