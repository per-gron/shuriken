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

int NinjaMain::toolQuery(int argc, char *argv[]) {
  if (argc == 0) {
    error("expected a target to query");
    return 1;
  }

  for (int i = 0; i < argc; ++i) {
    Node *node;
    try {
      node = collectTarget(argv[i]);
    } catch (const BuildError &error) {
      error("%s", error.what());
      return 1;
    }

    printf("%s:\n", node->path().c_str());
    if (Edge *edge = node->in_edge()) {
      printf("  input: %s\n", edge->rule_->name().c_str());
      for (int in = 0; in < (int)edge->inputs_.size(); in++) {
        const char *label = "";
        if (edge->is_implicit(in)) {
          label = "| ";
        }
        else if (edge->is_order_only(in)) {
          label = "|| ";
        }
        printf("    %s%s\n", label, edge->inputs_[in]->path().c_str());
      }
    }
    printf("  outputs:\n");
    for (const auto &edge = node->out_edges()) {
      for (const auto &out = edge->outputs_) {
        printf("    %s\n", out->path().c_str());
      }
    }
  }
  return 0;
}
