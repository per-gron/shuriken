#pragma once

namespace shk {

enum class DependencyType {
  ALWAYS,
  /**
   * This boolean instructs the build system to not log the file as a dependency
   * if the path points to a directory.
   *
   * This isn't exactly the epitome of elegance. The reason it is here is that
   * the sandbox parser receives input data in a form where it cannot
   * distinguish between reading a file's metadata (which makes that file a
   * dependency) vs reading a directory's metadata (which should not make that
   * file a dependency, because then lots of directories that shouldn't be
   * dependencies are in this list, for example /, /etc, /tmp, the build
   * folder).
   */
  IGNORE_IF_DIRECTORY,
};

}  // namespace shk
