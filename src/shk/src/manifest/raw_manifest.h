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
#include <vector>

#include "fs/file_system.h"
#include "manifest/raw_step.h"
#include "parse_error.h"

namespace shk {

/**
 * A RawManifest is an object that has the same information as a Ninja manifest,
 * in more or less the same structure, only in a C++ object. It is not without
 * preprocessing good to use as a base to do a build.
 */
struct RawManifest {
  std::vector<RawStep> steps;
  std::vector<Path> defaults;
  std::unordered_map<std::string, int> pools;

  /**
   * The build directory, used for storing the invocation log.
   */
  std::string build_dir;

  /**
   * Paths to all the manifest files that were read parsing this Manifest.
   */
  std::vector<std::string> manifest_files;
};

/**
 * Parse a Ninja manifest file at the given path.
 */
RawManifest parseManifest(
    Paths &paths,
    FileSystem &file_system,
    const std::string &path) throw(IoError, ParseError);

}  // namespace shk
