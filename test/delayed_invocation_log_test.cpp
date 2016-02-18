#include <catch.hpp>

#include <stdexcept>

#include "delayed_invocation_log.h"
#include "in_memory_file_system.h"
#include "in_memory_invocation_log.h"
#include "path.h"

namespace shk {

TEST_CASE("DelayedInvocationLog") {
  time_t now = 234;
  const auto clock = [&] { return now; };

  InMemoryFileSystem fs;
  auto memory_log_ptr = std::unique_ptr<InMemoryInvocationLog>(
      new InMemoryInvocationLog(fs, clock));
  auto &memory_log = *memory_log_ptr;
  DelayedInvocationLog log(clock, std::move(memory_log_ptr));

  Hash hash_a;
  std::fill(hash_a.data.begin(), hash_a.data.end(), 123);
  Hash hash_b;
  std::fill(hash_b.data.begin(), hash_b.data.end(), 321);

  SECTION("CreatedDirectory") {
    log.createdDirectory("foo");
    CHECK(memory_log.createdDirectories().count("foo") == 1);
  }

  SECTION("RemovedDirectory") {
    log.createdDirectory("foo");
    log.removedDirectory("foo");
    CHECK(memory_log.createdDirectories().count("foo") == 0);
  }

  SECTION("RanCommand") {
    SECTION("DelayWrite") {
      log.ranCommand(hash_a, {}, {});
      CHECK(memory_log.entries().count(hash_a) == 0);
    }

    SECTION("WriteLater") {
      log.ranCommand(hash_a, {}, {});
      now++;
      log.ranCommand(hash_b, {}, {});
      CHECK(memory_log.entries().count(hash_a) == 1);
      CHECK(memory_log.entries().count(hash_b) == 0);
    }

    SECTION("WriteSeveralLater") {
      log.ranCommand(hash_a, {}, {});
      log.ranCommand(hash_b, {}, {});
      now++;
      log.ranCommand(hash_a, {}, {});
      CHECK(memory_log.entries().count(hash_a) == 1);
      CHECK(memory_log.entries().count(hash_b) == 1);
    }

    SECTION("WriteOnlyOnce") {
      log.ranCommand(hash_a, {}, {});
      now++;
      log.ranCommand(hash_b, {}, {});
      memory_log.cleanedCommand(hash_a);
      now++;
      log.ranCommand(hash_b, {}, {});  // This should not write hash_a again
      CHECK(memory_log.entries().count(hash_a) == 0);
    }
  }

  SECTION("CleanedCommand") {
    memory_log.ranCommand(hash_a, {}, {});
    memory_log.ranCommand(hash_b, {}, {});

    SECTION("DelayWrite") {
      log.cleanedCommand(hash_a);
      CHECK(memory_log.entries().count(hash_a) == 1);
    }

    SECTION("WriteLater") {
      log.cleanedCommand(hash_a);
      now++;
      log.cleanedCommand(hash_b);
      CHECK(memory_log.entries().count(hash_a) == 0);
    }

    SECTION("WriteSeveralLater") {
      log.cleanedCommand(hash_a);
      log.cleanedCommand(hash_b);
      now++;
      log.cleanedCommand(hash_a);
      CHECK(memory_log.entries().count(hash_a) == 0);
      CHECK(memory_log.entries().count(hash_b) == 0);
    }

    SECTION("WriteOnlyOnce") {
      log.cleanedCommand(hash_a);
      now++;
      log.cleanedCommand(hash_b);
      memory_log.ranCommand(hash_a, {}, {});
      now++;
      log.cleanedCommand(hash_b);  // This should not write hash_a again
      CHECK(memory_log.entries().count(hash_a) == 1);
    }
  }

  SECTION("WriteAll") {
    SECTION("FlushPendingWrites") {
      memory_log.ranCommand(hash_a, {}, {});
      memory_log.ranCommand(hash_b, {}, {});
      log.writeAll();
      CHECK(memory_log.entries().count(hash_a) == 1);
      CHECK(memory_log.entries().count(hash_b) == 1);
    }
  }

  log.writeAll();
}

}  // namespace shk
