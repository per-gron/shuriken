#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "file_system.h"
#include "parse_error.h"
#include "step.h"

namespace shk {

struct Manifest {
  std::vector<Step> steps;
  std::vector<Path> defaults;
  std::unordered_map<std::string, int> pools;
};

/**
 * Parse a Ninja manifest file at the given path.
 */
Manifest parseManifest(
    FileSystem &file_system,
    const std::string &path) throw(IoError, ParseError);

}  // namespace shk
