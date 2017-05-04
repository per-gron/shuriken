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

#include "log/invocation_log.h"

namespace shk {

class DummyInvocationLog : public InvocationLog {
 public:
  void createdDirectory(nt_string_view path)
      throw(IoError) override {}

  void removedDirectory(nt_string_view path)
      throw(IoError) override {}

  std::pair<Fingerprint, FileId> fingerprint(const std::string &path) override {
    return std::make_pair(Fingerprint(), FileId());
  }

  void ranCommand(
      const Hash &build_step_hash,
      std::vector<std::string> &&output_files,
      std::vector<Fingerprint> &&output_fingerprints,
      std::vector<std::string> &&input_files,
      std::vector<Fingerprint> &&input_fingerprints,
      std::vector<uint32_t> &&ignored_dependencies,
      std::vector<Hash> &&additional_dependencies)
          throw(IoError) override {}

  void cleanedCommand(
      const Hash &build_step_hash) throw(IoError) override {}
};

}  // namespace shk
