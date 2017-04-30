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

#include "log/invocations.h"

#include <atomic>
#include <thread>

#include "util.h"

namespace shk {
namespace {

/**
 * The return value has one entry per worker thread. Each vector<bool> is a
 * "map" from fingerprint index (in the fingerprints vector) to a boolean
 * indicating whether it is used by one of the entries in entry_vec.
 */
std::vector<std::vector<bool>> findUsedFingerprints(
    const std::vector<std::pair<nt_string_view, Fingerprint>> &fingerprints,
    const std::vector<const Invocations::Entry *> &entry_vec) {
  const int num_threads = guessParallelism();
  std::vector<std::vector<bool>> used_fingerprints(num_threads);
  std::atomic<int> next_entry(0);
  std::vector<std::thread> threads;
  for (int i = 0; i < num_threads; i++) {
    threads.emplace_back(
        [i, &entry_vec, &next_entry, &used_fingerprints, &fingerprints] {
          used_fingerprints[i].resize(fingerprints.size());
          auto &fps = used_fingerprints[i];

          for (;;) {
            int entry_idx = next_entry++;
            if (entry_idx >= entry_vec.size()) {
              break;
            }
            const auto *entry = entry_vec[entry_idx];
            for (const auto idx : entry->output_files) {
              fps[idx] = true;
            }
            for (const auto idx : entry->input_files) {
              fps[idx] = true;
            }
          }
        });
  }
  for (auto &thread : threads) {
    thread.join();
  }

  return used_fingerprints;
}

}  // anonymous namespace

int Invocations::countUsedFingerprints() const {
  std::vector<const Entry *> entry_vec;
  for (const auto &entry : entries) {
    entry_vec.push_back(&entry.second);
  }

  const auto used_fingerprints = findUsedFingerprints(fingerprints, entry_vec);

  int count = 0;
  for (int i = 0; i < fingerprints.size(); i++) {
    for (int j = 0; j < used_fingerprints.size(); j++) {
      if (used_fingerprints[j][i]) {
        count++;
        break;
      }
    }
  }

  return count;
}

std::vector<uint32_t> Invocations::fingerprintsFor(
    const std::vector<const Entry *> &entries) const {
  const auto used_fingerprints = findUsedFingerprints(fingerprints, entries);

  std::vector<uint32_t> ans;
  for (int i = 0; i < fingerprints.size(); i++) {
    for (int j = 0; j < used_fingerprints.size(); j++) {
      if (used_fingerprints[j][i]) {
        ans.push_back(i);
        break;
      }
    }
  }

  return ans;
}

bool operator==(
    const Invocations::Entry &a, const Invocations::Entry &b) {
  return
      a.output_files == b.output_files &&
      a.input_files == b.input_files;
}

bool operator!=(
    const Invocations::Entry &a, const Invocations::Entry &b) {
  return !(a == b);
}

bool operator==(const Invocations &a, const Invocations &b) {
  if (a.created_directories != b.created_directories) {
    return false;
  }

  if (a.entries.size() != b.entries.size()) {
    return false;
  }

  for (const auto &a_entry : a.entries) {
    const auto b_it = b.entries.find(a_entry.first);
    if (b_it == b.entries.end()) {
      return false;
    }

    const auto files_are_same = [&](
        const std::vector<std::pair<nt_string_view, Fingerprint>> &a_fps,
        const std::vector<std::pair<nt_string_view, Fingerprint>> &b_fps,
        FingerprintIndicesView a_files,
        FingerprintIndicesView b_files) {
      if (a_files.size() != b_files.size()) {
        return false;
      }

      for (int i = 0; i < a_files.size(); i++) {
        if (a_fps[a_files[i]] != b_fps[b_files[i]]) {
          return false;
        }
      }
      return true;
    };

    if (!files_are_same(
            a.fingerprints,
            b.fingerprints,
            a_entry.second.output_files,
            b_it->second.output_files) ||
        !files_are_same(
            a.fingerprints,
            b.fingerprints,
            a_entry.second.input_files,
            b_it->second.input_files)) {
      return false;
    }
  }

  return true;
}

bool operator!=(const Invocations &a, const Invocations &b) {
  return !(a == b);
}

}  // namespace shk
