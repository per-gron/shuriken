#pragma once

#include <unordered_set>
#include <vector>

#include "parse_error.h"
#include "path.h"

namespace shk {

struct SandboxResult {
  std::unordered_set<Path> created;
  std::unordered_set<Path> read;
  /**
   * A list of human readable strings that describe things that the process did
   * that is disallowed, for example network access, mounting a file system or
   * if it modified, moved or deleted a file that it did not create.
   */
  std::vector<std::string> violations;
};

inline bool operator==(const SandboxResult &a, const SandboxResult &b) {
  return (
      a.created == b.created &&
      a.read == b.read &&
      a.violations == b.violations);
}

/**
 * Parse the output of the (trace "...") statement in sandboxes. This can be
 * used for dependency tracking. Please note that this parser only supports a
 * very limited subset of the full sandbox format. Sandbox files are really
 * programs in (a modified version of) TinyScheme.
 *
 * Throws ParseError when it encounters syntax that it doesn't support.
 */
SandboxResult parseSandbox(
    Paths &paths, std::string &&contents) throw(ParseError);

}  // namespace shk
