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

#include "in_memory_file_system.h"
#include "in_memory_invocation_log.h"
#include "log/invocations.h"

namespace shk {

TEST_CASE("InMemoryInvocationLog") {
  InMemoryFileSystem fs;
  std::string err;
  CHECK(fs.writeFile("test_file", "hello!", &err));
  CHECK(err == "");

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

  SECTION("LeakMemory") {
    CHECK(!log.hasLeakedMemory());
    log.leakMemory();
    CHECK(log.hasLeakedMemory());
  }

  SECTION("Commands") {
    SECTION("Empty") {
      log.ranCommand(hash, {}, {}, {}, {});
      CHECK(log.entries().count(hash) == 1);
      log.cleanedCommand(hash);
      CHECK(log.entries().empty());
    }

    SECTION("Input") {
      CHECK(fs.writeFile("file", "", &err));
      CHECK(err == "");
      log.ranCommand(
          hash, {}, {}, { "file" }, { takeFingerprint(fs, 0, "file").first });
      CHECK(log.entries().size() == 1);
      REQUIRE(log.entries().count(hash) == 1);
      const auto &entry = log.entries().begin()->second;
      CHECK(entry.output_files.empty());
      REQUIRE(entry.input_files.size() == 1);
      CHECK(entry.input_files[0].first == "file");
    }

    SECTION("InputsOutputsWithSharedFingerprints") {
      auto hash_2 = hash;
      hash_2.data[0]++;

      const auto fp = takeFingerprint(fs, 0, "file").first;
      log.ranCommand(
          hash, { "file" }, { fp }, { "file" }, { fp });
      log.ranCommand(
          hash_2, {}, {}, { "file" }, { fp });

      const auto invocations = log.invocations();
      REQUIRE(invocations.fingerprints.size() == 1);
      CHECK(
          invocations.fingerprints[0] ==
          (std::make_pair(nt_string_view("file"), fp)));
    }

    SECTION("IgnoreDir") {
      CHECK(fs.mkdir("dir") == IoError::success());
      log.ranCommand(
          hash, {}, {}, { "dir" }, { takeFingerprint(fs, 0, "dir").first });
      CHECK(log.entries().size() == 1);
      REQUIRE(log.entries().count(hash) == 1);
      const auto &entry = log.entries().begin()->second;
      CHECK(entry.output_files.empty());
      CHECK(entry.input_files.empty());
    }

    SECTION("OutputDir") {
      CHECK(fs.mkdir("dir") == IoError::success());
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
      CHECK(fs.mkdir("dir") == IoError::success());
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
      CHECK(fs.mkdir("a") == IoError::success());
      log.createdDirectory("a");

      std::unordered_map<FileId, nt_string_view> created_directories{
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
