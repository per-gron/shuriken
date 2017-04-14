// Copyright 2011 Google Inc. All Rights Reserved.
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

#include "build.h"
#include "tools/deps.h"

namespace shk {

int toolDeps(int argc, char **argv, const ToolParams &params) {
  const auto step_indices = computeStepsToBuild(
      params.paths, params.indexed_manifest, argc, argv);

  for (const auto step : params.indexed_manifest.steps) {
    if (step.phony()) {
      continue;
    }

    const auto entry_it = params.invocations.entries.find(step.hash);
    if (entry_it == params.invocations.entries.end()) {
      continue;
    }

    const auto &entry = entry_it->second;
    const auto &fingerprint = params.invocations.fingerprints;

    const auto file_to_str = [&](size_t idx) {
      const auto &file = fingerprint[idx];
      const auto &path = file.first;
      const auto &fp = file.second;
      const auto result = fingerprintMatches(params.file_system, path, fp);
      return path +
          (result.clean ? "" : " [dirty]") +
          (result.should_update ? " [should update]" : "");
    };

    printf("%s\n", step.command.c_str());

    bool first = true;
    for (const auto &output : entry.output_files) {
      printf("  %s%s", first ? "" : "\n", file_to_str(output).c_str());
      first = false;
    }
    if (first) {
      printf("[no output file]");
    }

    printf(
        ": #deps %lu\n",
        entry.input_files.size());
    for (const auto &input : entry.input_files) {
      printf("    %s\n", file_to_str(input).c_str());
    }
    printf("\n");
  }

  return 0;
}

}  // namespace shk
