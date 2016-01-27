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

int NinjaMain::toolTargets(int argc, char *argv[]) {
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
}
