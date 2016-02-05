#pragma once

#include <unordered_set>
#include <vector>

#include "parse_error.h"

namespace shk {

struct SandboxResult {
  /**
   * Contains the list of files that the process created and did not remove.
   * If the process creates a file and then moves it, this set contains only
   * the path that was moved to. These files can be seen as output files of the
   * command that ran. When cleaning, these files should be removed.
   */
  std::unordered_set<std::string> created;
  /**
   * Contains the list of files that were read. These can be seen as
   * dependencies of the command that ran. Files that were created are not
   * added to this set: They are not seen as input dependencies even if the
   * program reads them.
   */
  std::unordered_set<std::string> read;
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

struct SandboxIgnores {
  std::unordered_set<std::string> file_access;
  std::unordered_set<std::string> network_access;

  static SandboxIgnores defaults();
};

/**
 * Parse the output of the (trace "...") statement in sandboxes. This can be
 * used for dependency tracking. Please note that this parser only supports a
 * very limited subset of the full sandbox format. Sandbox files are really
 * programs in (a modified version of) TinyScheme.
 *
 * Throws ParseError when it encounters syntax that it doesn't support.
 */
SandboxResult parseSandbox(
    const SandboxIgnores &ignores,
    std::string &&contents) throw(ParseError);

}  // namespace shk
