// Copyright 2017 Per Grön. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <catch.hpp>

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
  CHECK(empty.parse_data.fingerprint_entry_count == 0);
  CHECK(empty.parse_data.path_entry_count == 0);
  CHECK(!empty.parse_data.buffer);
}

void writeFile(FileSystem &fs, nt_string_view path, string_view contents) {
  std::string err;
  CHECK(fs.writeFile(path, contents, &err));
  CHECK(err == "");
}

void ranCommand(
    InvocationLog &log,
    const Hash &build_step_hash,
    std::vector<std::string> &&output_files,
    std::vector<std::string> &&input_files) {
  log.ranCommand(
      build_step_hash,
      std::move(output_files),
      log.fingerprintFiles(output_files),
      std::move(input_files),
      log.fingerprintFiles(input_files));
}

/**
 * Test that committing a set of entries to the log and reading it back does
 * the same thing as just writing those entries to an Invocations object.
 */
template <typename Callback>
void roundtrip(const Callback &callback) {
  InMemoryFileSystem fs;
  InMemoryInvocationLog in_memory_log(fs, [] { return 0; });
  const auto persistent_log = openPersistentInvocationLog(
      fs,
      [] { return 0; },
      "file",
      InvocationLogParseResult::ParseData());
  callback(*persistent_log, fs);
  callback(in_memory_log, fs);
  auto result = parsePersistentInvocationLog(fs, "file");

  auto in_memory_result = in_memory_log.invocations();

  CHECK(result.warning == "");
  CHECK(in_memory_result == result.invocations);
}

template <typename Callback>
void leak(const Callback &callback) {
  InMemoryFileSystem fs;
  const auto persistent_log = openPersistentInvocationLog(
      fs,
      [] { return 0; },
      "file",
      InvocationLogParseResult::ParseData());
  callback(*persistent_log, fs);
  persistent_log->leakMemory();

  // There isn't really anything to CHECK here. It should not crash.
}

template <typename Callback>
void multipleWriteCycles(const Callback &callback, InMemoryFileSystem fs) {
  InMemoryInvocationLog in_memory_log(fs, [] { return 0; });
  callback(in_memory_log, fs);
  for (size_t i = 0; i < 5; i++) {
    auto result = parsePersistentInvocationLog(fs, "file");
    CHECK(result.warning == "");
    const auto persistent_log = openPersistentInvocationLog(
        fs,
        [] { return 0; },
        "file",
        std::move(result.parse_data));
    callback(*persistent_log, fs);
  }

  auto result = parsePersistentInvocationLog(fs, "file");

  auto in_memory_result = in_memory_log.invocations();

  CHECK(result.warning == "");
  CHECK(in_memory_result == result.invocations);
}

template <typename Callback>
void shouldEventuallyRequestRecompaction(const Callback &callback) {
  InMemoryFileSystem fs;
  for (size_t attempts = 0;; attempts++) {
    const auto persistent_log = openPersistentInvocationLog(
        fs,
        [] { return 0; },
        "file",
        InvocationLogParseResult::ParseData());
    callback(*persistent_log, fs);
    const auto result = parsePersistentInvocationLog(fs, "file");
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

template <typename Callback>
void recompact(const Callback &callback, int run_times = 5) {
  InMemoryFileSystem fs;
  InMemoryInvocationLog in_memory_log(fs, [] { return 0; });
  callback(in_memory_log, fs);
  for (size_t i = 0; i < run_times; i++) {
    auto result = parsePersistentInvocationLog(fs, "file");
    CHECK(result.warning == "");
    const auto persistent_log = openPersistentInvocationLog(
        fs,
        [] { return 0; },
        "file",
        std::move(result.parse_data));
    callback(*persistent_log, fs);
  }
  auto parse_data = recompactPersistentInvocationLog(
      fs,
      [] { return 0; },
      parsePersistentInvocationLog(fs, "file").invocations,
      "file");

  auto result = parsePersistentInvocationLog(fs, "file");
  CHECK(!result.needs_recompaction);

  auto in_memory_result = in_memory_log.invocations();

  CHECK(result.warning == "");

  CHECK(in_memory_result == result.invocations);

  // Sanity check parse_data
  CHECK(parse_data.buffer);
  CHECK(
      parse_data.fingerprint_entry_count ==
      result.invocations.fingerprints.size());
  CHECK(parse_data.path_entry_count == parse_data.path_ids.size());

  for (const auto &fingerprint : in_memory_result.fingerprints) {
    const auto path = std::string(fingerprint.first);
    CHECK(parse_data.path_ids.count(path));
    CHECK(parse_data.fingerprint_ids.count(path));
  }

  std::unordered_set<uint32_t> fingerprint_entry_ids;
  std::unordered_set<uint32_t> path_entry_ids;
  const auto add_entry_id = [&](std::unordered_set<uint32_t> &set, size_t id) {
    bool no_id_duplicate = set.emplace(id).second;
    CHECK(no_id_duplicate);
  };
  for (const auto &fingerprint : in_memory_result.fingerprints) {
    const auto path = std::string(fingerprint.first);
    const auto path_id_it = parse_data.path_ids.find(path);
    if (path_id_it != parse_data.path_ids.end()) {
      add_entry_id(path_entry_ids, path_id_it->second);
    }
    parse_data.path_ids.erase(path);

    const auto fingerprint_id_it = parse_data.fingerprint_ids.find(path);
    if (fingerprint_id_it != parse_data.fingerprint_ids.end()) {
      add_entry_id(fingerprint_entry_ids, fingerprint_id_it->second.record_id);
    }
    parse_data.fingerprint_ids.erase(path);
  }
  CHECK(parse_data.path_ids.empty());  // path_ids contains extraenous entries
  CHECK(  // fingerprint_ids contains extraenous entries
      parse_data.fingerprint_ids.empty());

  for (const auto &dir : in_memory_result.created_directories) {
    CHECK(parse_data.path_ids.count(dir.second));
  }

  // These checks are just here for getting more detailed information in case
  // the invocations check above fails.
  CHECK(in_memory_result.fingerprints == result.invocations.fingerprints);
  CHECK(in_memory_result.entries == result.invocations.entries);
  CHECK(
      in_memory_result.created_directories ==
      result.invocations.created_directories);
}

template <typename Callback>
void warnOnTruncatedInput(const Callback &callback) {
  InMemoryFileSystem fs;

  const size_t kFileSignatureSize = 16;

  fs.open("file", "w");  // Just to make the initial unlink work
  size_t warnings = 0;

  for (size_t i = 1;; i++) {
    // Truncate byte by byte until only the signature is left. This should
    // never crash or fail, only warn.

    CHECK(fs.unlink("file") == IoError::success());
    const auto persistent_log = openPersistentInvocationLog(
        fs, [] { return 0; }, "file", InvocationLogParseResult::ParseData());
    callback(*persistent_log, fs);
    const auto stat = fs.stat("file");
    const auto truncated_size = stat.metadata.size - i;
    if (truncated_size <= kFileSignatureSize) {
      break;
    }
    CHECK(fs.truncate("file", truncated_size) == IoError::success());
    const auto result = parsePersistentInvocationLog(fs, "file");
    if (result.warning != "") {
      warnings++;
    }

    // parsePersistentInvocationLog should have truncated the file now
    const auto result_after = parsePersistentInvocationLog(fs, "file");
    CHECK(result_after.warning == "");
  }

  CHECK(warnings > 0);
}

template <typename Callback>
void writeEntries(const Callback &callback) {
  roundtrip(callback);
  leak(callback);
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

  Hash hash_0;
  std::fill(hash_0.data.begin(), hash_0.data.end(), 0);
  Hash hash_1;
  std::fill(hash_0.data.begin(), hash_0.data.end(), 1);

  SECTION("Parsing") {
    SECTION("Missing") {
      parsePersistentInvocationLog(fs, "missing");
    }

    SECTION("Empty") {
      writeFile(fs, "empty", "");
      auto result = parsePersistentInvocationLog(fs, "empty");
      CHECK(
          result.warning ==
          "invalid invocation log file signature (too short)");
      CHECK(fs.stat("empty").result == ENOENT);
    }

    SECTION("InvalidHeader") {
      writeFileWithHeader(fs, "invalid_header", 3);
      auto result = parsePersistentInvocationLog(fs, "invalid_header");
      CHECK(
          result.warning ==
          "invalid invocation log file version or bad byte order");
      CHECK(fs.stat("invalid_header").result == ENOENT);
    }

    SECTION("JustHeader") {
      writeFileWithHeader(fs, "just_header", 1);
      checkEmpty(parsePersistentInvocationLog(fs, "just_header"));
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

      ranCommand(
          *persistent_log, hash_0, {}, { "dir" });

      const auto result = parsePersistentInvocationLog(fs, "file");
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
        writeFile(fs, "test_file", "hello!");
        CHECK(
            log.fingerprint("test_file") ==
            takeFingerprint(fs, 0, "test_file"));
        ranCommand(
            log,
            hash_0,
            { "test_file" },
            {});
      });
    }

    SECTION("InvocationNoFiles") {
      writeEntries([&](InvocationLog &log, FileSystem &fs) {
        ranCommand(log, hash_0, {}, {});
      });
    }

    SECTION("InvocationSingleInputFile") {
      writeEntries([&](InvocationLog &log, FileSystem &fs) {
        ranCommand(log, hash_0, {}, { "hi" });
      });
    }

    SECTION("InvocationTwoInputFiles") {
      writeEntries([&](InvocationLog &log, FileSystem &fs) {
        ranCommand(
            log,
            hash_0,
            {},
            { "hi", "duh" });
      });
    }

    SECTION("InvocationSingleOutputFile") {
      writeEntries([&](InvocationLog &log, FileSystem &fs) {
        ranCommand(log, hash_0, { "hi" }, {});
      });
    }

    SECTION("InvocationSingleInputDir") {
      fs.mkdir("dir");
      multipleWriteCycles([&](InvocationLog &log, FileSystem &fs) {
        ranCommand(log, hash_0, {}, { "dir" });
      }, fs);
    }

    SECTION("InvocationSingleOutputDir") {
      fs.mkdir("dir");
      multipleWriteCycles([&](InvocationLog &log, FileSystem &fs) {
        ranCommand(log, hash_0, { "dir" }, {});
      }, fs);
    }

    SECTION("InvocationSingleOutputFileAndDir") {
      fs.mkdir("dir");
      multipleWriteCycles([&](InvocationLog &log, FileSystem &fs) {
        ranCommand(log, hash_0, { "dir", "hi" }, {});
      }, fs);
    }

    SECTION("InvocationTwoOutputFiles") {
      writeEntries([&](InvocationLog &log, FileSystem &fs) {
        ranCommand(log, hash_0, { "aah", "hi" }, {});
      });
    }

    SECTION("InvocationInputAndOutputFiles") {
      writeEntries([&](InvocationLog &log, FileSystem &fs) {
        ranCommand(log, hash_0, { "aah" }, { "hi" });
      });
    }

    SECTION("InvocationDifferentFingerprintsSameStep") {
      // This test requires recompaction to work; PersistentInvocationLog
      // will not remove the overwritten fingerprints until a recompaction.
      recompact([&](InvocationLog &log, FileSystem &fs) {
        for (int i = 0; i < 2; i++) {
          std::vector<std::string> output_files = { "aah" };

          auto output_fingerprints = log.fingerprintFiles(output_files);
          for (int j = 0; j < output_files.size(); j++) {
            output_fingerprints[0].hash.data[0] = i;
          }

          log.ranCommand(
              hash_0,
              std::move(output_files),
              std::move(output_fingerprints),
              {},
              {});
        }
      });
    }

    SECTION("InvocationDifferentStepsSameFingerprints") {
      writeEntries([&](InvocationLog &log, FileSystem &fs) {
        writeFile(fs, "ooh", "");
        writeFile(fs, "iih", "");

        for (int i = 0; i < 2; i++) {
          std::vector<std::string> output_files = { "aah", "ooh" };

          auto output_fingerprints = log.fingerprintFiles(output_files);
          hash_0.data.data()[0] = i;
          log.ranCommand(
              hash_0,
              std::move(output_files),
              std::move(output_fingerprints),
              {},
              {});
        }
      });
    }

    SECTION("InvocationsWithLotsOfDifferentFingerprints") {
      // If the needs_recompaction logic is inaccurate, it might be possible to
      // trigger a state where needs_recompaction is true immediately after a
      // recompaction. This test tries to trigger that.

      recompact([&](InvocationLog &log, FileSystem &fs) {
        writeFile(fs, "ooh", "ooh");
        writeFile(fs, "iih", "iih");

        for (int i = 0; i < 3000; i++) {
          std::vector<std::string> output_files = { "aah" , "ooh", "iih" };

          auto output_fingerprints = log.fingerprintFiles(output_files);
          for (int j = 0; j < output_files.size(); j++) {
            *reinterpret_cast<int *>(
                output_fingerprints[j].hash.data.data()) = i;
          }

          *reinterpret_cast<int *>(hash_0.data.data()) = i;
          log.ranCommand(
              hash_0,
              std::move(output_files),
              std::move(output_fingerprints),
              {},
              {});
        }
      }, /*run_times:*/1);
    }

    SECTION("OverwrittenInvocation") {
      writeEntries([&](InvocationLog &log, FileSystem &fs) {
        ranCommand(log, hash_0, {}, {});
        ranCommand(log, hash_0, { "hi" }, {});
      });
    }

    SECTION("DeletedMissingInvocation") {
      writeEntries([&](InvocationLog &log, FileSystem &fs) {
        log.cleanedCommand(hash_0);
      });
    }

    SECTION("DeletedInvocation") {
      writeEntries([&](InvocationLog &log, FileSystem &fs) {
        ranCommand(log, hash_0, {}, {});
        log.cleanedCommand(hash_0);
      });
    }

    SECTION("MixAndMatch") {
      writeEntries([&](InvocationLog &log, FileSystem &fs) {
        log.createdDirectory("dir");
        log.createdDirectory("dir_2");
        log.removedDirectory("dir");

        ranCommand(log, hash_0, { "hi" }, { "aah" });
        log.cleanedCommand(hash_1);
        ranCommand(log, hash_1, {}, {});
        log.cleanedCommand(hash_0);
      });
    }
  }
}

}  // namespace shk
