#include <catch.hpp>

#include <stdexcept>

#include "log/delayed_invocation_log.h"
#include "log/persistent_invocation_log.h"

#include "../in_memory_file_system.h"
#include "../in_memory_invocation_log.h"

namespace shk {

TEST_CASE("DelayedInvocationLog") {
  time_t now = 234;
  const auto clock = [&] { return now; };

  InMemoryFileSystem fs(clock);
  fs.writeFile("test_file", "hello!");

  auto memory_log_ptr = std::unique_ptr<InMemoryInvocationLog>(
      new InMemoryInvocationLog(fs, clock));
  auto &memory_log = *memory_log_ptr;
  const auto log = delayedInvocationLog(clock, std::move(memory_log_ptr));

  Hash hash_a;
  std::fill(hash_a.data.begin(), hash_a.data.end(), 123);
  Hash hash_b;
  std::fill(hash_b.data.begin(), hash_b.data.end(), 321);

  SECTION("CreatedDirectory") {
    log->createdDirectory("foo");
    CHECK(memory_log.createdDirectories().count("foo") == 1);
  }

  SECTION("RemovedDirectory") {
    log->createdDirectory("foo");
    log->removedDirectory("foo");
    CHECK(memory_log.createdDirectories().count("foo") == 0);
  }

  SECTION("Fingerprint") {
    CHECK(
        log->fingerprint("test_file") ==
        takeFingerprint(fs, now, "test_file"));
  }

  SECTION("RanCommand") {
    SECTION("DelayWrite") {
      log->ranCommand(hash_a, {}, {}, {}, {});
      CHECK(memory_log.entries().count(hash_a) == 0);
    }

    SECTION("WriteLater") {
      log->ranCommand(hash_a, {}, {}, {}, {});
      now++;
      log->ranCommand(hash_b, {}, {}, {}, {});
      CHECK(memory_log.entries().count(hash_a) == 1);
      CHECK(memory_log.entries().count(hash_b) == 0);
    }

    SECTION("WriteSeveralLater") {
      log->ranCommand(hash_a, {}, {}, {}, {});
      log->ranCommand(hash_b, {}, {}, {}, {});
      now++;
      log->ranCommand(hash_a, {}, {}, {}, {});
      CHECK(memory_log.entries().count(hash_a) == 1);
      CHECK(memory_log.entries().count(hash_b) == 1);
    }

    SECTION("WriteOnlyOnce") {
      log->ranCommand(hash_a, {}, {}, {}, {});
      now++;
      log->ranCommand(hash_b, {}, {}, {}, {});
      memory_log.cleanedCommand(hash_a);
      now++;
      log->ranCommand(hash_b, {}, {}, {}, {});  // Should not write hash_a again
      CHECK(memory_log.entries().count(hash_a) == 0);
    }

    SECTION("WriteOutputs") {
      auto fingerprint = takeFingerprint(fs, now, "test_file").first;
      CHECK(fingerprint.racily_clean);  // Make sure that it gets to unset this
      log->ranCommand(
          hash_a,
          { "test_file" },
          { fingerprint },
          {},
          {});
      now++;
      log->ranCommand(hash_b, {}, {}, {}, {});
      REQUIRE(memory_log.entries().count(hash_a) == 1);
      const auto &entry = memory_log.entries().find(hash_a)->second;

      CHECK(entry.input_files.empty());

      fingerprint.racily_clean = false;
      CHECK(
          entry.output_files ==
          (std::vector<std::pair<std::string, Fingerprint>>({
              { "test_file", fingerprint } })));
    }

    SECTION("WriteInputs") {
      auto fingerprint = takeFingerprint(fs, now, "test_file").first;
      CHECK(fingerprint.racily_clean);  // Make sure that it gets to unset this
      log->ranCommand(
          hash_a,
          {},
          {},
          { "test_file" },
          { fingerprint });
      now++;
      log->ranCommand(hash_b, {}, {}, {}, {});
      REQUIRE(memory_log.entries().count(hash_a) == 1);
      const auto &entry = memory_log.entries().find(hash_a)->second;

      CHECK(entry.output_files.empty());

      fingerprint.racily_clean = false;
      CHECK(
          entry.input_files ==
          (std::vector<std::pair<std::string, Fingerprint>>({
              { "test_file", fingerprint } })));
    }
  }

  SECTION("CleanedCommand") {
    memory_log.ranCommand(hash_a, {}, {}, {}, {});
    memory_log.ranCommand(hash_b, {}, {}, {}, {});

    SECTION("DelayWrite") {
      log->cleanedCommand(hash_a);
      CHECK(memory_log.entries().count(hash_a) == 1);
    }

    SECTION("WriteLater") {
      log->cleanedCommand(hash_a);
      now++;
      log->cleanedCommand(hash_b);
      CHECK(memory_log.entries().count(hash_a) == 0);
    }

    SECTION("WriteSeveralLater") {
      log->cleanedCommand(hash_a);
      log->cleanedCommand(hash_b);
      now++;
      log->cleanedCommand(hash_a);
      CHECK(memory_log.entries().count(hash_a) == 0);
      CHECK(memory_log.entries().count(hash_b) == 0);
    }

    SECTION("WriteOnlyOnce") {
      log->cleanedCommand(hash_a);
      now++;
      log->cleanedCommand(hash_b);
      memory_log.ranCommand(hash_a, {}, {}, {}, {});
      now++;
      log->cleanedCommand(hash_b);  // This should not write hash_a again
      CHECK(memory_log.entries().count(hash_a) == 1);
    }
  }

  SECTION("LeakMemory") {
    CHECK(!memory_log.hasLeakedMemory());
    log->leakMemory();
    CHECK(memory_log.hasLeakedMemory());
  }

  SECTION("WriteAll") {
    SECTION("FlushPendingWrites") {
      {
        const auto log = delayedInvocationLog(
            clock,
            openPersistentInvocationLog(
                fs,
                clock,
                "shk.log",
                InvocationLogParseResult::ParseData()));
        log->ranCommand(hash_a, {}, {}, {}, {});
        log->ranCommand(hash_b, {}, {}, {}, {});

        // At this point, log is destroyed and it should write the remaining
        // pending writes.
      }

      const auto invocations = parsePersistentInvocationLog(
          fs,
          "shk.log").invocations;
      CHECK(invocations.entries.count(hash_a) == 1);
      CHECK(invocations.entries.count(hash_b) == 1);
    }
  }
}

}  // namespace shk
