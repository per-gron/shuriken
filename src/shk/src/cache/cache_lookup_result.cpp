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

#include "cache/cache_lookup_result.h"

namespace shk {

CacheLookupResult::CacheLookupResult(StepIndex num_steps)
    : _steps(num_steps) {}

CacheLookupResult::~CacheLookupResult() {
  for (auto &step : _steps) {
    if (step) {
      delete step.load();
    }
  }
}

void CacheLookupResult::insert(StepIndex index, Entry &&entry) {
  for (auto &input_file : entry.input_files) {
    input_file.first = _strings.get(std::string(input_file.first));
    input_file.second = &_hashes.get(*input_file.second);
  }

  Entry *previous_entry = _steps[index].exchange(
      new Entry(std::move(entry)),
      std::memory_order_acq_rel);
  delete previous_entry;
}

std::unique_ptr<CacheLookupResult::Entry> CacheLookupResult::pop(
    StepIndex index) {
  return std::unique_ptr<CacheLookupResult::Entry>(
      _steps[index].exchange(nullptr, std::memory_order_acq_rel));
}

}  // namespace shk
