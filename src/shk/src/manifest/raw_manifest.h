#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "fs/file_system.h"
#include "manifest/step.h"
#include "parse_error.h"

namespace shk {

/**
 * A RawManifest is an object that has the same information as a Ninja manifest,
 * in more or less the same structure, only in a C++ object. It is not without
 * preprocessing good to use as a base to do a build.
 */
struct RawManifest {
  std::vector<Step> steps;
  std::vector<Path> defaults;
  std::unordered_map<std::string, int> pools;

  /**
   * The build directory, used for storing the invocation log.
   */
  std::string build_dir;
};

/**
 * Parse a Ninja manifest file at the given path.
 */
RawManifest parseManifest(
    Paths &paths,
    FileSystem &file_system,
    const std::string &path) throw(IoError, ParseError);

}  // namespace shk
