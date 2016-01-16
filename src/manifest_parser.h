#pragma once

#include <string>
#include <vector>

#include "file_system.h"
#include "parse_error.h"
#include "step.h"

namespace shk {

/**
 * Parse a Ninja manifest file at the given path.
 */
std::vector<Steps> parseManifest(
    FileSystem::Stream &file_stream,
    const std::string &path) throw(IoError, ParseError);

}  // namespace shk
