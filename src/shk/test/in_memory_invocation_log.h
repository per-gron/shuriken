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

#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>

#include "clock.h"
#include "log/invocation_log.h"
#include "log/invocations.h"

namespace shk {

/**
 * An InvocationLog implementation that is memory backed rather than disk based
 * like the real InvocationLog. Used for testing and for dry runs.
 */
class InMemoryInvocationLog : public InvocationLog {
 public:
  struct Entry {
    std::vector<std::pair<std::string, Fingerprint>> output_files;
    std::vector<std::pair<std::string, Fingerprint>> input_files;
    std::vector<uint32_t> ignored_dependencies;
    std::vector<Hash> additional_dependencies;
  };

  InMemoryInvocationLog(FileSystem &file_system, const Clock &clock);

  void createdDirectory(nt_string_view path) throw(IoError) override;
  void removedDirectory(nt_string_view path) throw(IoError) override;
  std::pair<Fingerprint, FileId> fingerprint(const std::string &path) override;
  void ranCommand(
      const Hash &build_step_hash,
      std::vector<std::string> &&output_files,
      std::vector<Fingerprint> &&output_fingerprints,
      std::vector<std::string> &&input_files,
      std::vector<Fingerprint> &&input_fingerprints,
      std::vector<uint32_t> &&ignored_dependencies,
      std::vector<Hash> &&additional_dependencies)
          throw(IoError) override;
  void cleanedCommand(
      const Hash &build_step_hash) throw(IoError) override;
  void leakMemory() override;

  /**
   * Expose the contents of the in memory invocation log as an Invocations
   * object. This emulates what would happen if the invocation log would have
   * been read from disk.
   */
  Invocations invocations() const;

  const std::unordered_set<std::string> &createdDirectories() const {
    return _created_directories;
  }

  const std::unordered_map<Hash, Entry> &entries() const {
    return _entries;
  }

  bool hasLeakedMemory() const {
    return _has_leaked;
  }


 private:
  bool _has_leaked = false;
  FileSystem &_fs;
  const Clock _clock;
  std::unordered_map<Hash, Entry> _entries;
  std::unordered_set<std::string> _created_directories;
};

}  // namespace shk
