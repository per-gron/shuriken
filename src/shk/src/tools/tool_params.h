// Copyright 2011 Google Inc. All Rights Reserved.
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

#include "../clock.h"
#include "../log/invocations.h"
#include "../manifest/compiled_manifest.h"

namespace shk {

struct ToolParams {
  Clock clock;
  Paths &paths;
  const Invocations &invocations;
  const CompiledManifest &compiled_manifest;
  FileSystem &file_system;
  std::string invocation_log_path;
};

}  // namespace shk
