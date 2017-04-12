#include <catch.hpp>

#include "fs/path.h"
#include "log/persistent_invocation_log.h"

#include "../in_memory_file_system.h"
#include "../in_memory_invocation_log.h"

namespace shk {
namespace {

void checkEmpty(const InvocationLogParseResult &empty) {
  CHECK(empty.invocations.entries.empty());
  CHECK(empty.invocations.created_directories.empty());
  CHECK(empty.warning.empty());
  CHECK(!empty.needs_recompaction);
  CHECK(empty.parse_data.path_ids.empty());
  CHECK(empty.parse_data.fingerprint_ids.empty());
  CHECK(empty.parse_data.entry_count == 0);
}

void sortInvocations(Invocations &invocations) {
  for (auto &entry : invocations.entries) {
    std::sort(
        entry.second.output_files.begin(),
        entry.second.output_files.end());
    std::sort(
        entry.second.input_files.begin(),
        entry.second.input_files.end());
  }
}

/**
 * Test that committing a set of entries to the log and reading it back does
 * the same thing as just writing those entries to an Invocations object.
 */
template<typename Callback>
void roundtrip(const Callback &callback) {
  InMemoryFileSystem fs;
  Paths paths(fs);
  InMemoryInvocationLog in_memory_log(fs, [] { return 0; });
  const auto persistent_log = openPersistentInvocationLog(
      fs,
      [] { return 0; },
      "file",
      InvocationLogParseResult::ParseData());
  callback(*persistent_log, fs);
  callback(in_memory_log, fs);
  auto result = parsePersistentInvocationLog(paths, fs, "file");
  sortInvocations(result.invocations);

  auto in_memory_result = in_memory_log.invocations(paths);
  sortInvocations(in_memory_result);

  CHECK(result.warning == "");
  CHECK(in_memory_result == result.invocations);
}

template<typename Callback>
void multipleWriteCycles(const Callback &callback, InMemoryFileSystem fs) {
  Paths paths(fs);
  InMemoryInvocationLog in_memory_log(fs, [] { return 0; });
  callback(in_memory_log, fs);
  for (size_t i = 0; i < 5; i++) {
    auto result = parsePersistentInvocationLog(paths, fs, "file");
    CHECK(result.warning == "");
    const auto persistent_log = openPersistentInvocationLog(
        fs,
        [] { return 0; },
        "file",
        std::move(result.parse_data));
    callback(*persistent_log, fs);
  }

  auto result = parsePersistentInvocationLog(paths, fs, "file");
  sortInvocations(result.invocations);

  auto in_memory_result = in_memory_log.invocations(paths);
  sortInvocations(in_memory_result);

  CHECK(result.warning == "");
  CHECK(in_memory_result == result.invocations);
}

template<typename Callback>
void shouldEventuallyRequestRecompaction(const Callback &callback) {
  InMemoryFileSystem fs;
  Paths paths(fs);
  for (size_t attempts = 0;; attempts++) {
    const auto persistent_log = openPersistentInvocationLog(
        fs,
        [] { return 0; },
        "file",
        InvocationLogParseResult::ParseData());
    callback(*persistent_log, fs);
    const auto result = parsePersistentInvocationLog(paths, fs, "file");
    if (result.needs_recompaction) {
      CHECK(attempts > 10);  // Should not immediately request recompaction
      break;
    }
    if (attempts > 10000) {
      CHECK(!"Should eventually request recompaction");
      break;
    }
  }
}

template<typename Callback>
void recompact(const Callback &callback) {
  InMemoryFileSystem fs;
  Paths paths(fs);
  InMemoryInvocationLog in_memory_log(fs, [] { return 0; });
  callback(in_memory_log, fs);
  for (size_t i = 0; i < 5; i++) {
    auto result = parsePersistentInvocationLog(paths, fs, "file");
    CHECK(result.warning == "");
    const auto persistent_log = openPersistentInvocationLog(
        fs,
        [] { return 0; },
        "file",
        std::move(result.parse_data));
    callback(*persistent_log, fs);
  }
  recompactPersistentInvocationLog(
      fs,
      [] { return 0; },
      parsePersistentInvocationLog(paths, fs, "file").invocations,
      "file");

  auto result = parsePersistentInvocationLog(paths, fs, "file");
  sortInvocations(result.invocations);

  auto in_memory_result = in_memory_log.invocations(paths);
  sortInvocations(in_memory_result);

  CHECK(result.warning == "");
  CHECK(in_memory_result == result.invocations);
}

template<typename Callback>
void warnOnTruncatedInput(const Callback &callback) {
  InMemoryFileSystem fs;
  Paths paths(fs);

  const size_t kFileSignatureSize = 16;

  fs.open("file", "w");  // Just to make the initial unlink work
  size_t warnings = 0;

  for (size_t i = 1;; i++) {
    // Truncate byte by byte until only the signature is left. This should
    // never crash or fail, only warn.

    fs.unlink("file");
    const auto persistent_log = openPersistentInvocationLog(
        fs, [] { return 0; }, "file", InvocationLogParseResult::ParseData());
    callback(*persistent_log, fs);
    const auto stat = fs.stat("file");
    const auto truncated_size = stat.metadata.size - i;
    if (truncated_size <= kFileSignatureSize) {
      break;
    }
    fs.truncate("file", truncated_size);
    const auto result = parsePersistentInvocationLog(paths, fs, "file");
    if (result.warning != "") {
      warnings++;
    }

    // parsePersistentInvocationLog should have truncated the file now
    const auto result_after = parsePersistentInvocationLog(paths, fs, "file");
    CHECK(result_after.warning == "");
  }

  CHECK(warnings > 0);
}

template<typename Callback>
void writeEntries(const Callback &callback) {
  roundtrip(callback);
  shouldEventuallyRequestRecompaction(callback);
  multipleWriteCycles(callback, InMemoryFileSystem());
  recompact(callback);
  warnOnTruncatedInput(callback);
}

void writeFileWithHeader(
    FileSystem &fs,
    const std::string &file,
    uint32_t version) {
  const auto stream = fs.open(file, "w");
  const std::string kFileSignature = "invocations:";
  stream->write(
        reinterpret_cast<const uint8_t *>(kFileSignature.data()),
        kFileSignature.size(),
        1);
  stream->write(
        reinterpret_cast<const uint8_t *>(&version),
        sizeof(version),
        1);
}

}  // anonymous namespace

TEST_CASE("PersistentInvocationLog") {
  InMemoryFileSystem fs;
  Paths paths(fs);

  Hash hash_0;
  std::fill(hash_0.data.begin(), hash_0.data.end(), 0);
  Hash hash_1;
  std::fill(hash_0.data.begin(), hash_0.data.end(), 1);

  SECTION("Parsing") {
    SECTION("Missing") {
      parsePersistentInvocationLog(paths, fs, "missing");
    }

    SECTION("Empty") {
      fs.writeFile("empty", "");
      auto result = parsePersistentInvocationLog(paths, fs, "empty");
      CHECK(
          result.warning ==
          "invalid invocation log file signature (too short)");
      CHECK(fs.stat("empty").result == ENOENT);
    }

    SECTION("InvalidHeader") {
      writeFileWithHeader(fs, "invalid_header", 3);
      auto result = parsePersistentInvocationLog(paths, fs, "invalid_header");
      CHECK(
          result.warning ==
          "invalid invocation log file version or bad byte order");
      CHECK(fs.stat("invalid_header").result == ENOENT);
    }

    SECTION("JustHeader") {
      writeFileWithHeader(fs, "just_header", 1);
      checkEmpty(parsePersistentInvocationLog(paths, fs, "just_header"));
    }
  }

  SECTION("Writing") {
    SECTION("InvocationIgnoreInputDirectory") {
      fs.mkdir("dir");

      const auto persistent_log = openPersistentInvocationLog(
          fs,
          [] { return 0; },
          "file",
          InvocationLogParseResult::ParseData());

      persistent_log->ranCommand(
          hash_0, {}, { "dir" });

      const auto result = parsePersistentInvocationLog(paths, fs, "file");
      CHECK(result.warning == "");
      REQUIRE(result.invocations.entries.size() == 1);
      CHECK(result.invocations.entries.begin()->first == hash_0);
      CHECK(result.invocations.entries.begin()->second.output_files.empty());
      CHECK(result.invocations.entries.begin()->second.input_files.empty());
    }

    SECTION("Empty") {
      const auto callback = [](InvocationLog &log, FileSystem &fs) {};
      // Don't use the shouldEventuallyRequestRecompaction test
      roundtrip(callback);
      multipleWriteCycles(callback, InMemoryFileSystem());
    }

    SECTION("CreatedDirectory") {
      writeEntries([](InvocationLog &log, FileSystem &fs) {
        log.createdDirectory("dir");
      });
    }

    SECTION("CreatedThenDeletedDirectory") {
      writeEntries([](InvocationLog &log, FileSystem &fs) {
        log.createdDirectory("dir");
        log.removedDirectory("dir");
      });
    }

    SECTION("Fingerprint") {
      writeEntries([hash_0](InvocationLog &log, FileSystem &fs) {
        fs.writeFile("test_file", "hello!");
        CHECK(
            log.fingerprint("test_file") ==
            takeFingerprint(fs, 0, "test_file"));
        log.ranCommand(
            hash_0,
            { "test_file" },
            {});
      });
    }

    SECTION("InvocationNoFiles") {
      writeEntries([&](InvocationLog &log, FileSystem &fs) {
        log.ranCommand(hash_0, {}, {});
      });
    }

    SECTION("InvocationSingleInputFile") {
      writeEntries([&](InvocationLog &log, FileSystem &fs) {
        log.ranCommand(hash_0, {}, { "hi" });
      });
    }

    SECTION("InvocationTwoInputFiles") {
      writeEntries([&](InvocationLog &log, FileSystem &fs) {
        log.ranCommand(
            hash_0,
            {},
            { "hi", "duh" });
      });
    }

    SECTION("InvocationSingleOutputFile") {
      writeEntries([&](InvocationLog &log, FileSystem &fs) {
        log.ranCommand(hash_0, { "hi" }, {});
      });
    }

    SECTION("InvocationSingleInputDir") {
      fs.mkdir("dir");
      multipleWriteCycles([&](InvocationLog &log, FileSystem &fs) {
        log.ranCommand(hash_0, {}, { "dir" });
      }, fs);
    }

    SECTION("InvocationSingleOutputDir") {
      fs.mkdir("dir");
      multipleWriteCycles([&](InvocationLog &log, FileSystem &fs) {
        log.ranCommand(hash_0, { "dir" }, {});
      }, fs);
    }

    SECTION("InvocationSingleOutputFileAndDir") {
      fs.mkdir("dir");
      multipleWriteCycles([&](InvocationLog &log, FileSystem &fs) {
        log.ranCommand(hash_0, { "dir", "hi" }, {});
      }, fs);
    }

    SECTION("InvocationTwoOutputFiles") {
      writeEntries([&](InvocationLog &log, FileSystem &fs) {
        log.ranCommand(hash_0, { "aah", "hi" }, {});
      });
    }

    SECTION("InvocationInputAndOutputFiles") {
      writeEntries([&](InvocationLog &log, FileSystem &fs) {
        log.ranCommand(hash_0, { "aah" }, { "hi" });
      });
    }

    SECTION("OverwrittenInvocation") {
      writeEntries([&](InvocationLog &log, FileSystem &fs) {
        log.ranCommand(hash_0, {}, {});
        log.ranCommand(hash_0, { "hi" }, {});
      });
    }

    SECTION("DeletedMissingInvocation") {
      writeEntries([&](InvocationLog &log, FileSystem &fs) {
        log.cleanedCommand(hash_0);
      });
    }

    SECTION("DeletedInvocation") {
      writeEntries([&](InvocationLog &log, FileSystem &fs) {
        log.ranCommand(hash_0, {}, {});
        log.cleanedCommand(hash_0);
      });
    }

    SECTION("MixAndMatch") {
      writeEntries([&](InvocationLog &log, FileSystem &fs) {
        log.createdDirectory("dir");
        log.createdDirectory("dir_2");
        log.removedDirectory("dir");

        log.ranCommand(hash_0, { "hi" }, { "aah" });
        log.cleanedCommand(hash_1);
        log.ranCommand(hash_1, {}, {});
        log.cleanedCommand(hash_0);
      });
    }
  }
}

}  // namespace shk
