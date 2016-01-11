#pragma once

#include <string>
#include <vector>

namespace shk {

struct Result {
  std::vector<std::string> input_files;
  std::vector<std::string> output_files;
  int return_code;
  std::string stdout;
  std::string stderr;
  std::vector<std::string> linting_errors;
};

// FIXME(peck): Async?
using CommandRunner = std::function<Result (const std::string &command)>;

}  // namespace shk
