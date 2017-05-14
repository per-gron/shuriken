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
#include <vector>

#include <util/assert.h>
#include <util/string_view.h>

#include "cache/interner.h"
#include "hash.h"
#include "manifest/step.h"

namespace shk {

/**
 * CacheLookupResult is a map from StepIndex to successful persistent cache
 * lookup results. When created it is empty. It is then filled up gradually by
 * the cache lookup code.
 *
 * All methods on CacheLookupResult can be called concurrently, from any thread.
 *
 * The idea with CacheLookupResult is to be a central coordination point between
 * the (possibly concurrent) work of doing cache lookups and the actual build
 * work.
 */
class CacheLookupResult {
 public:
  struct Entry {
    std::vector<std::pair<std::string, Hash>> output_files;
    /**
     * The paths and hashes of the input files. They have non-owning references
     * to the actual string and hash objects for deduplication purposes, since
     * a very large number of Entry objects could refer to the same inputs.
     */
    std::vector<std::pair<nt_string_view, const Hash *>> input_files;

    /**
     * See Invocations::Entry::ignored_dependencies
     */
    std::vector<uint32_t> ignored_dependencies;

    /**
     * See Invocations::Entry::additional_dependencies
     */
    std::vector<Hash> additional_dependencies;
  };

  CacheLookupResult(StepIndex num_steps);
  ~CacheLookupResult();

  /**
   * Inserts an entry at a given index. If an entry already exists at the
   * specified position, the old entry is overwritten.
   *
   * index must be less than num_steps passed to the constructor.
   */
  void insert(StepIndex index, Entry &&entry);

  /**
   * Takes (and removes) an Entry. If no entry is found, returns nullptr.
   *
   * The reason this steals the entry is both to reclaim memory as soon as
   * possible and because it would be tricky to return a reference to an object
   * in this map, because it can be overwritten (and subsequently destroyed) at
   * any time.
   *
   * The object that may be returned by this method has unowning references to
   * strings and Hash objects that are alive as long as the CacheLookupResult
   * object.
   *
   * index must be less than num_steps passed to the constructor.
   */
  std::unique_ptr<Entry> pop(StepIndex index);

 private:
  Interner<Hash> _hashes;
  Interner<std::string> _strings;

  std::vector<std::atomic<Entry *>> _steps;
};

}  // namespace shk
