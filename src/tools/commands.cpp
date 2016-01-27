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

namespace shk {
namespace {

void printCommands(Edge *edge, std::set<Edge *> *seen) {
  if (!edge) {
    return;
  }
  if (!seen->insert(edge).second) {
    return;
  }

  for (const auto &in : edge->inputs_) {
    printCommands(in->in_edge(), seen);
  }

  if (!edge->is_phony()) {
    puts(edge->EvaluateCommand().c_str());
  }
}

}  // anonymous namespace

int toolCommands(int argc, char *argv[]) {
  std::vector<Node *> nodes;
  try {
    nodes = collectTargetsFromArgs(argc, argv);
  } catch (BuildError &error) {
    error("%s", error.what());
    return 1;
  }

  std::set<Edge *> seen;
  for (const auto &in : nodes) {
    printCommands(in->in_edge(), &seen);
  }

  return 0;
}

}  // namespace shk
