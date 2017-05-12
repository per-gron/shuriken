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

#include "cache_keys_client.h"

#include <thread>

#include "test.h"

namespace {

SHA1::Digest makeDigest(uint8_t id) {
  SHA1::Digest result;
  std::fill(result.begin(), result.end(), 0);
  result[0] = id;
  return result;
}

void makeDependenciesHelper(Dependencies *result) {
}

template<typename... Args>
void makeDependenciesHelper(
    Dependencies *result,
    uint8_t digest,
    Args... args) {
  result->push_back({
      makeDigest(digest),
      "path" + std::to_string(digest) });
  makeDependenciesHelper(result, args...);
}

template<typename... Args>
Dependencies makeDependencies(
    Args... args) {
  Dependencies result;
  makeDependenciesHelper(&result, args...);
  return result;
}

template<typename... Args>
FindSha1 makeDependenciesFindSha1(
    Args... args) {
  Dependencies deps = makeDependencies(args...);
  return [deps](
      const std::string &path_relative_to_cwd,
      std::array<uint8_t, 20> *out) {
    for (const auto &dep : deps) {
      if (dep.path == path_relative_to_cwd) {
        *out = dep.hash;
        return FindSha1Result::SUCCESS;
      }
    }
    return FindSha1Result::FAILURE;
  };
}

void makeKeysHelper(std::unordered_map<std::string, SHA1::Digest> *result) {
}

template<typename... Args>
void makeKeysHelper(
    std::unordered_map<std::string, SHA1::Digest> *result,
    const std::string &path,
    uint8_t digest,
    Args... args) {
  (*result)[path] = makeDigest(digest);
  makeKeysHelper(result, args...);
}

template<typename... Args>
std::unordered_map<std::string, SHA1::Digest> makeKeys(
    Args... args) {
  std::unordered_map<std::string, SHA1::Digest> result;
  makeKeysHelper(&result, args...);
  return result;
}

const char kTestDir[] = "cache-keys-tempdir";

std::vector<std::unique_ptr<CacheKeysClient>> makeCaches() {
  std::vector<std::unique_ptr<CacheKeysClient>> result;
  result.push_back(onDiskCacheKeysClient(kTestDir));
  return result;
}

struct CacheKeysClientTest : public testing::Test {
  virtual void SetUp() {
    // In case a crashing test left a stale file behind.
    CleanupTempDir();
  }
  virtual void TearDown() {
    CleanupTempDir();
  }

 private:
  void CleanupTempDir() {
    #ifdef _WIN32
      string command = "rmdir /s /q " + std::string(kTestDir);
    #else
      string command = "rm -rf " + std::string(kTestDir);
    #endif
    if (system(command.c_str()) < 0)
      Fatal("system: %s", strerror(errno));
  }
};

TEST_F(CacheKeysClientTest, EmptyLookupOnEmptyCache) {
  for (const auto &cache : makeCaches()) {
    const auto lookup = cache->lookup(
        std::chrono::system_clock::time_point(), {}, {});
    EXPECT_TRUE(lookup.file_id_entries.empty());
    EXPECT_TRUE(lookup.pruned_file_ids.empty());
  }
}

TEST_F(CacheKeysClientTest, LookupOnEmptyCache) {
  for (const auto &cache : makeCaches()) {
    const auto lookup = cache->lookup(
        std::chrono::system_clock::time_point(),
        makeDependenciesFindSha1(0),
        makeKeys("a", 0));
    EXPECT_TRUE(lookup.file_id_entries.empty());
    EXPECT_TRUE(lookup.pruned_file_ids.empty());
  }
}

TEST_F(CacheKeysClientTest, SaveAndLookupOne) {
  for (const auto &cache : makeCaches()) {
    const std::vector<FileToSave> files_to_save{
        { "file_id_0", makeDigest(0) },
        { "file_id_1", makeDigest(1) } };
    // 3, 2 to test unsorted calculated deps
    const auto calculated_deps = makeDependencies(3, 2);
    using std::chrono::system_clock;
    system_clock::time_point expiry(system_clock::duration(1234));

    cache->save(calculated_deps, files_to_save, expiry);

    // 3, 4, 2 to test unsorted potential deps
    auto lookup = cache->lookup(
        std::chrono::system_clock::time_point(),
        makeDependenciesFindSha1(3, 4, 2), makeKeys("a", 1));
    EXPECT_EQ(lookup.file_id_entries.size(), 1);
    EXPECT_EQ(lookup.file_id_entries["a"].calculated_dependencies, makeDependencies(2, 3));
    EXPECT_EQ(lookup.file_id_entries["a"].file_id, "file_id_1");
    EXPECT_TRUE(lookup.pruned_file_ids.empty());
  }
}

TEST_F(CacheKeysClientTest, SaveAndLookupTwo) {
  for (const auto &cache : makeCaches()) {
    const std::vector<FileToSave> files_to_save{
        { "file_id_0", makeDigest(0) },
        { "file_id_1", makeDigest(1) } };
    const auto calculated_deps = makeDependencies(2, 3);
    using std::chrono::system_clock;
    system_clock::time_point expiry(system_clock::duration(1234));

    cache->save(calculated_deps, files_to_save, expiry);

    const auto lookup = cache->lookup(
        std::chrono::system_clock::time_point(),
        makeDependenciesFindSha1(2, 3, 4), makeKeys("a", 1, "b", 0));
    EXPECT_EQ(lookup.file_id_entries.size(), 2);
    EXPECT_TRUE(lookup.pruned_file_ids.empty());
  }
}

TEST_F(CacheKeysClientTest, PruneExpiredEntries) {
  using std::chrono::system_clock;

  for (const auto &cache : makeCaches()) {
    const std::vector<FileToSave> files_to_save{
        { "file_id_0", makeDigest(0) } };
    system_clock::time_point expiry(system_clock::duration(1));

    cache->save(makeDependencies(), files_to_save, expiry);

    auto lookup = cache->lookup(
        system_clock::time_point(system_clock::duration(2)),
        makeDependenciesFindSha1(), makeKeys("a", 0));
    EXPECT_EQ(lookup.pruned_file_ids, std::vector<std::string>{ "file_id_0" });
    EXPECT_TRUE(lookup.file_id_entries.empty());
  }
}

TEST_F(CacheKeysClientTest, DontPruneNonExpiredEntries) {
  // There was a bug where the timestamps were stored in the wrong endianness
  // which caused leveldb's lexicographical ordering go nuts and do the
  // wrong thing.

  using std::chrono::system_clock;

  for (const auto &cache : makeCaches()) {
    const std::vector<FileToSave> files_to_save{
        { "file_id_0", makeDigest(0) } };
    ;

    cache->save(
        makeDependencies(),
        files_to_save,
        system_clock::time_point(system_clock::duration(0x101)));

    auto lookup = cache->lookup(
        system_clock::time_point(system_clock::duration(0x002)),
        makeDependenciesFindSha1(), makeKeys("a", 0));
    EXPECT_EQ(lookup.file_id_entries.size(), 1);
    EXPECT_TRUE(lookup.pruned_file_ids.empty());
  }
}

TEST_F(CacheKeysClientTest, SaveWithSameKeysAndLookup) {
  // TODO(peck)
}

TEST_F(CacheKeysClientTest, LookupWithConflict) {
  // TODO(peck)
}

// TODO(peck): Test onDiskCacheKeysClient

}  // anonymous namespace
