#pragma once

#include <string>

namespace shk {

struct CmdlineOptions {
  enum class Result {
    SUCCESS,
    VERSION,
    HELP
  };

  std::string tracefile;
  std::string command;
  Result result = Result::SUCCESS;
  bool suicide_when_orphaned = false;
  bool server = false;
  std::string replay;
  std::string capture;
  bool json = false;

  static CmdlineOptions parse(int argc, char *argv[]);
};

}