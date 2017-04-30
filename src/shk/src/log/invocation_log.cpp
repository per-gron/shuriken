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

#include "log/invocation_log.h"

namespace shk {

void InvocationLog::leakMemory() {
}

std::vector<Fingerprint> InvocationLog::fingerprintFiles(
    const std::vector<std::string> &files) {
  std::vector<Fingerprint> ans;
  ans.reserve(files.size());
  for (const auto &file : files) {
    ans.push_back(fingerprint(file).first);
  }
  return ans;
}

}  // namespace shk
