#include <catch.hpp>

#include "path.h"
#include "persistent_invocation_log.h"

#include "in_memory_file_system.h"
#include "in_memory_invocation_log.h"

namespace shk {

inline bool operator==(
    const InvocationLog::Entry &a,
    const InvocationLog::Entry &b) {
  return
      std::tie(a.output_files, a.input_files) ==
      std::tie(b.output_files, b.input_files);
}

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

void checkMatches(const InMemoryInvocationLog &log, const Invocations &invocations) {
  std::unordered_set<std::string> created_directories;
  for (const auto &dir : invocations.created_directories) {
    created_directories.insert(dir.original());
  }
  CHECK(log.createdDirectories() == created_directories);

  std::unordered_map<Hash, InvocationLog::Entry> entries;
  for (const auto &entry : invocations.entries) {
    InvocationLog::Entry log_entry;
    const auto files = [&](
        const std::vector<std::pair<Path, Fingerprint>> &files) {
      std::vector<std::pair<std::string, Fingerprint>> result;
      for (const auto &file : files) {
        result.emplace_back(file.first.original(), file.second);
      }
      return result;
    };
    log_entry.output_files = files(entry.second.output_files);
    log_entry.input_files = files(entry.second.input_files);
    entries[entry.first] = log_entry;
  }
  CHECK(log.entries() == entries);
}

/**
 * Test that committing a set of entries to the log and reading it back does
 * the same thing as just writing those entries to an Invocations object.
 */
template<typename Callback>
void roundtrip(const Callback &callback) {
  InMemoryFileSystem fs;
  Paths paths(fs);
  InMemoryInvocationLog in_memory_log;
  const auto persistent_log = openPersistentInvocationLog(
      fs,
      "file",
      InvocationLogParseResult::ParseData());
  callback(*persistent_log);
  callback(in_memory_log);
  const auto result = parsePersistentInvocationLog(paths, fs, "file");

  CHECK(result.warning == "");
  checkMatches(in_memory_log, result.invocations);
}

template<typename Callback>
void multipleWriteCycles(const Callback &callback) {
  InMemoryFileSystem fs;
  Paths paths(fs);
  InMemoryInvocationLog in_memory_log;
  callback(in_memory_log);
  for (size_t i = 0; i < 5; i++) {
    auto result = parsePersistentInvocationLog(paths, fs, "file");
    CHECK(result.warning == "");
    const auto persistent_log = openPersistentInvocationLog(
        fs,
        "file",
        std::move(result.parse_data));
    callback(*persistent_log);
  }

  const auto result = parsePersistentInvocationLog(paths, fs, "file");
  CHECK(result.warning == "");
  checkMatches(in_memory_log, result.invocations);
}

template<typename Callback>
void shouldEventuallyRequestRecompaction(const Callback &callback) {
  InMemoryFileSystem fs;
  Paths paths(fs);
  for (size_t attempts = 0;; attempts++) {
    const auto persistent_log = openPersistentInvocationLog(
        fs,
        "file",
        InvocationLogParseResult::ParseData());
    callback(*persistent_log);
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
  InMemoryInvocationLog in_memory_log;
  callback(in_memory_log);
  for (size_t i = 0; i < 5; i++) {
    auto result = parsePersistentInvocationLog(paths, fs, "file");
    CHECK(result.warning == "");
    const auto persistent_log = openPersistentInvocationLog(
        fs,
        "file",
        std::move(result.parse_data));
    callback(*persistent_log);
  }
  recompactPersistentInvocationLog(
      fs,
      parsePersistentInvocationLog(paths, fs, "file").invocations,
      "file");

  const auto result = parsePersistentInvocationLog(paths, fs, "file");
  CHECK(result.warning == "");
  checkMatches(in_memory_log, result.invocations);
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
        fs, "file", InvocationLogParseResult::ParseData());
    callback(*persistent_log);
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
  multipleWriteCycles(callback);
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
  fs.writeFile("empty", "");

  Hash hash_0;
  std::fill(hash_0.data.begin(), hash_0.data.end(), 0);
  Hash hash_1;
  std::fill(hash_0.data.begin(), hash_0.data.end(), 1);
  Fingerprint fp_0;
  std::fill(fp_0.hash.data.begin(), fp_0.hash.data.end(), 0);
  fp_0.timestamp = 1;
  Fingerprint fp_1;
  std::fill(fp_1.hash.data.begin(), fp_1.hash.data.end(), 0);
  fp_1.timestamp = 2;

  SECTION("Parsing") {
    parsePersistentInvocationLog(paths, fs, "missing");
    CHECK_THROWS_AS(
        parsePersistentInvocationLog(paths, fs, "empty"), ParseError);

    writeFileWithHeader(fs, "invalid_header", 3);
    CHECK_THROWS_AS(
        parsePersistentInvocationLog(paths, fs, "invalid_header"), ParseError);

    writeFileWithHeader(fs, "just_header", 1);
    checkEmpty(parsePersistentInvocationLog(paths, fs, "just_header"));
  }

  SECTION("Writing") {
    SECTION("Empty") {
      const auto callback = [](InvocationLog &log) {};
      // Don't use the shouldEventuallyRequestRecompaction test
      roundtrip(callback);
      multipleWriteCycles(callback);
    }

    SECTION("CreatedDirectory") {
      writeEntries([](InvocationLog &log) {
        log.createdDirectory("dir");
      });
    }

    SECTION("CreatedThenDeletedDirectory") {
      writeEntries([](InvocationLog &log) {
        log.createdDirectory("dir");
        log.removedDirectory("dir");
      });
    }

    SECTION("InvocationNoFiles") {
      writeEntries([&](InvocationLog &log) {
        InvocationLog::Entry entry;
        log.ranCommand(hash_0, entry);
      });
    }

    SECTION("InvocationSingleInputFile") {
      writeEntries([&](InvocationLog &log) {
        InvocationLog::Entry entry;
        entry.input_files.emplace_back("hi", fp_0);
        log.ranCommand(hash_0, entry);
      });
    }

    SECTION("InvocationTwoInputFiles") {
      writeEntries([&](InvocationLog &log) {
        InvocationLog::Entry entry;
        entry.input_files.emplace_back("hi", fp_0);
        entry.input_files.emplace_back("duh", fp_1);
        log.ranCommand(hash_0, entry);
      });
    }

    SECTION("InvocationSingleOutputFile") {
      writeEntries([&](InvocationLog &log) {
        InvocationLog::Entry entry;
        entry.output_files.emplace_back("hi", fp_0);
        log.ranCommand(hash_0, entry);
      });
    }

    SECTION("InvocationTwoOutputFiles") {
      writeEntries([&](InvocationLog &log) {
        InvocationLog::Entry entry;
        entry.output_files.emplace_back("aah", fp_0);
        entry.output_files.emplace_back("hi", fp_1);
        log.ranCommand(hash_0, entry);
      });
    }

    SECTION("InvocationInputAndOutputFiles") {
      writeEntries([&](InvocationLog &log) {
        InvocationLog::Entry entry;
        entry.input_files.emplace_back("aah", fp_0);
        entry.output_files.emplace_back("hi", fp_1);
        log.ranCommand(hash_0, entry);
      });
    }

    SECTION("OverwrittenInvocation") {
      writeEntries([&](InvocationLog &log) {
        InvocationLog::Entry entry;
        log.ranCommand(hash_0, entry);
        entry.output_files.emplace_back("hi", fp_0);
        log.ranCommand(hash_0, entry);
      });
    }

    SECTION("DeletedMissingInvocation") {
      writeEntries([&](InvocationLog &log) {
        log.cleanedCommand(hash_0);
      });
    }

    SECTION("DeletedInvocation") {
      writeEntries([&](InvocationLog &log) {
        InvocationLog::Entry entry;
        log.ranCommand(hash_0, entry);
        log.cleanedCommand(hash_0);
      });
    }

    SECTION("MixAndMatch") {
      writeEntries([&](InvocationLog &log) {
        log.createdDirectory("dir");
        log.createdDirectory("dir_2");
        log.removedDirectory("dir");

        InvocationLog::Entry entry;
        entry.input_files.emplace_back("aah", fp_0);
        entry.output_files.emplace_back("hi", fp_1);
        log.ranCommand(hash_0, entry);
        log.cleanedCommand(hash_1);
        log.ranCommand(hash_1, InvocationLog::Entry());
        log.cleanedCommand(hash_0);
      });
    }
  }
}

}  // namespace shk
