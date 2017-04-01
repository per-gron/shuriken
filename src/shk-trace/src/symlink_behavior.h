#pragma once

namespace shk {

/**
 * When file events happen (for example opening a file for writing or creating a
 * hardlink), they sometimes implicitly follow a symlink (for example when
 * open-ing a file without the O_NOFOLLOW flag) and sometimes they don't. This
 * enum tells if it does or not.
 *
 * It is not a bool because I think bools make for less self-documenting code
 * and it is in its own file because it is used in several otherwise unrelated
 * parts of the code.
 */
enum class SymlinkBehavior {
  FOLLOW,
  NO_FOLLOW
};

}  // namespace shk
