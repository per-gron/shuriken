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

#include "cmdline_options.h"

#include <getopt.h>

namespace shk {
namespace {

CmdlineOptions withResult(CmdlineOptions::Result result) {
  CmdlineOptions ans;
  ans.result = result;
  return ans;
}

}  // anonymous namespace

CmdlineOptions CmdlineOptions::parse(int argc, char *argv[]) {
  enum { OPT_VERSION = 1 };
  const option kLongOpts[] = {
    { "help", no_argument, nullptr, 'h' },
    { "version", no_argument, nullptr, OPT_VERSION },
    { "suicide-when-orphaned", no_argument, nullptr, 'O' },
    { "server", no_argument, nullptr, 's' },
    { "json", no_argument, nullptr, 'j' },
    { "capture", required_argument, nullptr, 'C' },
    { "replay", required_argument, nullptr, 'r' },
    { nullptr, 0, nullptr, 0 }
  };

  CmdlineOptions options;

  // Reset globals before using getopt_long (such a nice API...)
  optarg = nullptr;
  optind = 0;
  opterr = 0;
  optopt = 0;

  int opt;
  while ((opt = getopt_long(argc, argv, "+sjOf:c:C:r:", kLongOpts, nullptr)) != -1) {
    if (opt == 'f' || opt == 'c') {
      auto &target = opt == 'f' ? options.tracefile : options.command;
      if (*optarg == '\0' || !target.empty()) {
        return withResult(Result::HELP);
      }
      target = optarg;
    } else if (opt == OPT_VERSION) {
      return withResult(Result::VERSION);
    } else if (opt == 'C') {
      if (*optarg == '\0' || !options.capture.empty()) {
        return withResult(Result::HELP);
      }
      options.capture = optarg;
    } else if (opt == 'r') {
      if (*optarg == '\0' || !options.replay.empty()) {
        return withResult(Result::HELP);
      }
      options.replay = optarg;
    } else if (opt == 'O') {
      options.suicide_when_orphaned = true;
    } else if (opt == 's') {
      options.server = true;
    } else if (opt == 'j') {
      options.json = true;
    } else {
      return withResult(Result::HELP);
    }
  }

  if (optind != argc) {
    return withResult(Result::HELP);
  }

  if (options.server) {
    if (options.json ||
        !options.command.empty() ||
        !options.tracefile.empty() ||
        !options.replay.empty()) {
      return withResult(Result::HELP);
    }
  } else if (!options.replay.empty()) {
    if (options.suicide_when_orphaned ||
        options.json ||
        !options.command.empty() ||
        !options.tracefile.empty() ||
        !options.capture.empty()) {
      return withResult(Result::HELP);
    }
  } else {
    if (options.tracefile.empty()) {
      options.tracefile = "/dev/null";
    }

    if (options.suicide_when_orphaned ||
        options.command.empty() ||
        !options.capture.empty()) {
      return withResult(Result::HELP);
    }
  }

  return options;
}

}  // namespace shk
