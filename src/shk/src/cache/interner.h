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

#include <mutex>
#include <thread>
#include <unordered_set>

namespace shk {

/**
 * Interner is a thread-safe class that interns/deduplicates strings or other
 * objects.
 */
template <
    typename T,
    typename Ref = const T &>
class Interner {
 public:
  /**
   * Takes a reference to an object and checks an equal object is already in the
   * internal storage. If so, returns a reference to that. If not, it copies the
   * object to internal storage and returns a reference to the newly created
   * object.
   *
   * This method may be called concurrently from any thread.
   */
  const T &get(Ref ref) {
    std::lock_guard<std::mutex> lock(_values_mutex);
    return *_values.emplace(ref).first;
  }

 private:
  std::mutex _values_mutex;  // Protects _values
  std::unordered_set<T> _values;
};

}  // namespace shk
