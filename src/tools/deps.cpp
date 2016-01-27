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

int NinjaMain::toolDeps(int argc, char **argv) {
  std::vector<Node *> nodes;
  if (argc == 0) {
    for (const auto &n : _deps_log.nodes()) {
      if (_deps_log.IsDepsEntryLiveFor(n)) {
        nodes.push_back(n);
      }
    }
  } else {
    try {
      nodes = collectTargetsFromArgs(argc, argv);
    } catch (const BuildError &error) {
      error("%s", error.what());
      return 1;
    }
  }

  RealDiskInterface disk_interface;
  for (const auto &node : nodes) {
    DepsLog::Deps *deps = _deps_log.GetDeps(node);
    if (!deps) {
      printf("%s: deps not found\n", node->path().c_str());
      continue;
    }

    std::string err;
    TimeStamp mtime = disk_interface.Stat(node->path(), &err);
    if (mtime == -1) {
      error("%s", err.c_str());  // Log and ignore Stat() errors;
    }
    printf(
        "%s: #deps %d, deps mtime %d (%s)\n",
        node->path().c_str(), deps->node_count, deps->mtime,
        (!mtime || mtime > deps->mtime ? "STALE":"VALID"));
    for (int i = 0; i < deps->node_count; ++i) {
      printf("    %s\n", deps->nodes[i]->path().c_str());
    }
    printf("\n");
  }

  return 0;
}
