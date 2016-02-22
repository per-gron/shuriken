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

#include "targets.h"

namespace shk {
#if 0
namespace {

int toolTargetsList(const std::vector<Node *> &nodes, int depth, int indent) {
  for (const auto &n : nodes) {
    for (int i = 0; i < indent; ++i) {
      printf("  ");
    }
    const char *target = node->path().c_str();
    if (node->in_edge()) {
      printf("%s: %s\n", target, node->in_edge()->rule_->name().c_str());
      if (depth > 1 || depth <= 0) {
        toolTargetsList(node->in_edge()->inputs_, depth - 1, indent + 1);
      }
    } else {
      printf("%s\n", target);
    }
  }
  return 0;
}

int toolTargetsSourceList(State *state) {
  for (const auto &edge : state->edges_) {
    for (const auto &inp : edge->inputs_) {
      if (!inp->in_edge()) {
        printf("%s\n", inp->path().c_str());
      }
    }
  }
  return 0;
}

int toolTargetsList(State *state, const std::string &rule_name) {
  std::set<std::string> rules;

  // Gather the outputs.
  for (const auto &edge : state->edges_) {
    if (edge->rule_->name() == rule_name) {
      for (const auto &out_node : edge->outputs_) {
        rules.insert(out_node->path());
      }
    }
  }

  // Print them.
  for (auto i = rules.begin(); i != rules.end(); ++i) {
    printf("%s\n", (*i).c_str());
  }

  return 0;
}

int toolTargetsList(State *state) {
  for (const auto &edge : state->edges_) {
    for (const auto &out_node : edge->outputs_) {
      printf(
          "%s: %s\n",
          out_node->path().c_str(),
          edge->rule_->name().c_str());
    }
  }
  return 0;
}

}  // anonymous namespace
#endif

int toolTargets(int argc, char *argv[], const ToolParams &params) {
#if 0
  int depth = 1;
  if (argc >= 1) {
    std::string mode = argv[0];
    if (mode == "rule") {
      std::string rule;
      if (argc > 1) {
        rule = argv[1];
      }
      if (rule.empty()) {
        return toolTargetsSourceList(&_state);
      } else {
        return toolTargetsList(&_state, rule);
      }
    } else if (mode == "depth") {
      if (argc > 1) {
        depth = atoi(argv[1]);
      }
    } else if (mode == "all") {
      return toolTargetsList(&_state);
    } else {
      const char *suggestion =
          spellcheckString(mode.c_str(), "rule", "depth", "all", NULL);
      if (suggestion) {
        error("unknown target tool mode '%s', did you mean '%s'?",
              mode.c_str(), suggestion);
      } else {
        error("unknown target tool mode '%s'", mode.c_str());
      }
      return 1;
    }
  }

  std::string err;
  std::vector<Node *> root_nodes = _state.RootNodes(&err);
  if (err.empty()) {
    return toolTargetsList(root_nodes, depth, 0);
  } else {
    error("%s", err.c_str());
    return 1;
  }
#endif

  return 0;
}

}  // namespace shk
