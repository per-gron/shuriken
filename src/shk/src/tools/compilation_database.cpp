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

#include "compilation_database.h"

namespace shk {
#if 0
namespace {

void encodeJSONString(const char *str) {
  while (*str) {
    if (*str == '"' || *str == '\\') {
      putchar('\\');
    }
    putchar(*str);
    str++;
  }
}

}  // anonymous namespace
#endif

int toolCompilationDatabase(int argc, char *argv[], const ToolParams &params) {
#if 0
  bool first = true;
  std::vector<char> cwd;

  do {
    cwd.resize(cwd.size() + 1024);
    errno = 0;
  } while (!getcwd(&cwd[0], cwd.size()) && errno == ERANGE);
  if (errno != 0 && errno != ERANGE) {
    error("cannot determine working directory: %s", strerror(errno));
    return 1;
  }

  putchar('[');
  for (const auto &e : _state.edges_) {
    if (e->inputs_.empty()) {
      continue;
    }
    for (int i = 0; i != argc; ++i) {
      if (e->rule_->name() == argv[i]) {
        if (!first) {
          putchar(',');
        }

        printf("\n  {\n    \"directory\": \"");
        encodeJSONString(&cwd[0]);
        printf("\",\n    \"command\": \"");
        encodeJSONString(e->EvaluateCommand().c_str());
        printf("\",\n    \"file\": \"");
        encodeJSONString(e->inputs_[0]->path().c_str());
        printf("\"\n  }");

        first = false;
      }
    }
  }

  puts("\n]");
#endif

  return 0;
}

}  // namespace shk
