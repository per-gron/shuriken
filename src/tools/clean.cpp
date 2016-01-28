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

int toolClean(int argc, char *argv[]) {
#if 0
  // The clean tool uses getopt, and expects argv[0] to contain the name of
  // the tool, i.e. "clean".
  argc++;
  argv--;

  bool clean_rules = false;

  optind = 1;
  int opt;
  while ((opt = getopt(argc, argv, const_cast<char*>("hr"))) != -1) {
    switch (opt) {
    case 'r':
      clean_rules = true;
      break;
    case 'h':
    default:
      printf(
          "usage: ninja -t clean [options] [targets]\n"
          "\n"
          "options:\n"
          "  -r     interpret targets as a list of rules to clean instead\n");
    return 1;
    }
  }
  argv += optind;
  argc -= optind;

  if (clean_rules && argc == 0) {
    error("expected a rule to clean");
    return 1;
  }

  Cleaner cleaner(&_state, _config);
  if (argc >= 1) {
    if (clean_rules) {
      return cleaner.cleanRules(argc, argv);
    } else {
      return cleaner.cleanTargets(argc, argv);
    }
  } else {
    return cleaner.cleanAll();
  }

#endif
  return 0;
}

}  // namespace shk
