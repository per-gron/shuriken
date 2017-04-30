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

#include "manifest/raw_step.h"

#include <blake2.h>

namespace shk {

Hash RawStep::hash() const {
  Hash hash;
  blake2b_state state;
  blake2b_init(&state, hash.data.size());
  const auto hash_string = [&](const std::string &string) {
    blake2b_update(
        &state,
        reinterpret_cast<const uint8_t *>(string.data()),
        string.size() + 1);  // Include trailing \0
  };

  const auto hash_paths = [&](const std::vector<Path> &paths) {
    for (const auto &path : paths) {
      hash_string(path.original());
    }
    // Add a separator, so that it is impossible to get the same hash by just
    // removing a path from the end of one list and adding it to the beginning
    // of the next.
    hash_string("");  // "" is not a valid path so it is a good separator
  };
  hash_paths(inputs);
  hash_paths(implicit_inputs);
  hash_paths(dependencies);
  hash_paths(outputs);
  hash_string(generator ? "" : command);
  hash_string(rspfile);
  hash_string(rspfile_content);

  blake2b_final(&state, hash.data.data(), hash.data.size());
  return hash;
}

}  // namespace shk
