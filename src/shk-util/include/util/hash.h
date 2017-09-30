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

#include <array>

namespace shk {

static constexpr size_t kHashSize = 20;

/**
 * The reason Hash isn't a simple using type alias is to be able to define a
 * std::hash function for it.
 */
struct Hash {
  std::array<uint8_t, kHashSize> data;
};

inline bool operator==(const Hash &a, const Hash &b) {
  return a.data == b.data;
}

inline bool operator!=(const Hash &a, const Hash &b) {
  return !(a == b);
}

inline bool operator<(const Hash &a, const Hash &b) {
  return a.data < b.data;
}

}  // namespace shk

namespace std {

template<>
struct hash<shk::Hash> {
  using argument_type = shk::Hash;
  using result_type = std::size_t;

  result_type operator()(const argument_type &h) const {
    return *reinterpret_cast<const result_type *>(h.data.data());
  }
};

}  // namespace std
