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
    { nullptr, 0, nullptr, 0 }
  };

  CmdlineOptions options;

  // Reset globals before using getopt_long (such a nice API...)
  optarg = nullptr;
  optind = 0;
  opterr = 0;
  optopt = 0;

  int opt;
  while ((opt = getopt_long(argc, argv, "+f:c:", kLongOpts, nullptr)) != -1) {
    if (opt == 'f' || opt == 'c') {
      auto &target = opt == 'f' ? options.tracefile : options.command;
      if (*optarg == '\0' || !target.empty()) {
        return withResult(Result::HELP);
      }
      target = optarg;
    } else if (opt == OPT_VERSION) {
      return withResult(Result::VERSION);
    } else {
      return withResult(Result::HELP);
    }
  }

  if (options.tracefile.empty()) {
    options.tracefile = "/dev/null";
  }

  if (options.command.empty() || optind != argc) {
    return withResult(Result::HELP);
  }

  return options;
}

}  // namespace shk
