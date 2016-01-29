// Copyright 2011 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <string>
#include <unordered_set>

#include "file_system.h"
#include "path_error.h"
#include "string_piece.h"

namespace shk {

/**
 * Split a path into its dirname and basename. The first element in the
 * pair is the dirname, the second the basename.
 *
 * Acts like the dirname and basename functions in the standard library.
 */
std::pair<StringPiece, StringPiece> basenameSplitPiece(const std::string &path);

/**
 * Canonicalize a path like "foo/../bar.h" into just "bar.h".
 */
void canonicalizePath(std::string *path) throw(PathError);
void canonicalizePath(char *path, size_t *len) throw(PathError);

namespace detail {

struct CanonicalizedPath {
  explicit CanonicalizedPath(
    ino_t ino,
    dev_t dev,
    std::string &&path)
    : ino(ino), dev(dev), path(path) {}
  CanonicalizedPath(const CanonicalizedPath &) = delete;
  CanonicalizedPath &operator=(const CanonicalizedPath &) = delete;
  CanonicalizedPath(CanonicalizedPath &&) = default;
  CanonicalizedPath &operator=(CanonicalizedPath &&) = default;

  ino_t ino;
  dev_t dev;
  std::string path;
};

inline bool operator==(const CanonicalizedPath &a, const CanonicalizedPath &b) {
  return (
    a.ino == b.ino &&
    a.dev == b.dev &&
    a.path == b.path);
}

inline bool operator!=(const CanonicalizedPath &a, const CanonicalizedPath &b) {
  return !(a == b);
}

}  // namespace detail

class Paths;

/**
 * Object that represents a file system path that can be efficiently compared
 * with other Path objects. A Path object lives only as long as its parent Paths
 * object!
 */
class Path {
  friend struct std::hash<Path>;
 public:
  Path(const detail::CanonicalizedPath *canonicalized_path,
       const std::string *original_path)
      : _canonicalized_path(canonicalized_path),
        _original_path(original_path) {}

  /**
   * Returns true if the paths point to the same paths. operator== is not
   * used for this because this does not take the original path into account.
   */
  bool isSame(const Path &other) const {
    return _canonicalized_path == other._canonicalized_path;
  }

  /**
   * The original, non-canonicalized path. Always absolute or relative to the
   * current working directory.
   */
  const std::string &original() const {
    return *_original_path;
  }

  /**
   * Compare two path objects and see if they are equal. To be counted as equal
   * the objects have to have the same original path, it is not enough to have
   * the same canonicalized path. This is important to be able to use Path
   * objects in hash tables and sets.
   */
  bool operator==(const Path &other) const {
    return (
        _canonicalized_path == other._canonicalized_path &&
        _original_path == other._original_path);
  }

  bool operator!=(const Path &other) const {
    return !(*this == other);
  }

  /**
   * Comparison operator, suitable for use in maps and sets but the
   * comparison is dependent on memory layout so is not stable across runs.
   */
  bool operator<(const Path &other) const {
    return (
        std::tie(_canonicalized_path, _original_path) <
        std::tie(other._canonicalized_path, other._original_path));
  }

 private:
  const detail::CanonicalizedPath *_canonicalized_path;
  const std::string *_original_path;
};

}  // namespace shk

namespace std {

template<>
struct hash<shk::detail::CanonicalizedPath> {
  using argument_type = shk::detail::CanonicalizedPath;
  using result_type = std::size_t;

  result_type operator()(const argument_type &p) const {
    return std::hash<std::string>()(p.path);
  }
};

template<>
struct hash<shk::Path> {
  using argument_type = shk::Path;
  using result_type = std::size_t;

  result_type operator()(const argument_type &p) const {
    // Hash the pointer
    auto ad = reinterpret_cast<uintptr_t>(p._canonicalized_path);
    return static_cast<size_t>(ad ^ (ad >> 16));
  }
};

}  // namespace std

namespace shk {

/**
 * Shuriken manifests contain lots of paths. In order to accurately track
 * dependencies between build steps, it is necessary to accurately detect when
 * two paths contained in the manifest point to the same underlying file. Doing
 * this in an efficient way is a little bit tricky. Solving that problem is the
 * job of this class.
 *
 * One part of the problem is that it is necessary to be able to compare two
 * paths efficiently. This is solved by providing a special Path class instead
 * of using plain strings. Path objects are interned, meaning that instead of
 * containing the path directly, it has a pointer to an object with the path.
 * Paths guarantees that these objects are unique, so to compare a path it is
 * sufficient to compare the pointer in the Path object.
 *
 * The other half of the problem is that it is necessary to ensure that
 * CanonicalizedPath (the type that Path objects have a pointer to) objects are
 * unique for any given file. There are several aspects to this:
 *
 * Links (symbolic and hard): If there is a link from a to b, the paths a/c.txt
 * and b/c.txt point to the same file. Detecting this requires consulting the
 * file system. What Paths does here is to stat all paths and record the st_ino
 * and st_dev fields of the stat result. If st_ino and st_dev are the same, then
 * the paths are known to point to the same file, even if links are playing
 * tricks with us.
 *
 * Using st_ino and st_dev alone is not sufficient, though, because the manifest
 * often (always in the case of a clean build) contains paths to files that do
 * not yet exist. When Paths encounters a path that does not exist, it gets the
 * dirname of the path and tries to stat that, over and over again until it
 * finds an existing path. It then stores the st_ino and st_dev fields along
 * with the path after it. This is what a CanonicalizedPath object contains.
 *
 * Other aspects that must be taken into consideration when checking if two
 * paths point to the same file are absolute vs relative paths, path
 * canonicalization and case folding.
 *
 * Absolute vs relative paths: If the current working directory is /a, then the
 * paths b.txt and /a/b.txt point to the same file. Like symbolic links, solving
 * this problem requires system calls (at the very least to find the current
 * working directory). One way to solve it could be to absolutize all relative
 * paths by prepending the working directory. Shuriken does not do this though,
 * because the solution mentioned above to deal with links solves this issue
 * too.
 *
 * Path canonicalization: The path a/../c.txt and the path c.txt actually point
 * to the same file. A tempting solution for this problem is to simply
 * canonicalize all paths as a string manipulation operation. However, in the
 * presence of symbolic links, canonicalization is not correct.
 *
 * Consider the following directory structure:
 *
 * /
 * ├── 1
 * │   └── 2
 * │       ├── 3
 * │       └── x.txt
 * └── a
 *     ├── x.txt
 *     └── symlink -> ../1/2/3
 *
 * In this situation, /a/symlink/../x.txt refers to /1/2/x.txt, but if it would
 * be canonicalized first, it would point to /a/x.txt.
 *
 * To avoid this problem, Paths only canonicalizes the part of the path that
 * does not exist. Later on, when building, Shuriken decides what directories it
 * will have to create in advance. Later, when it actually creates them, it
 * fails the build if some build step has already created the directory.
 * Otherwise, there is a chance that the created directory is a link, which
 * would violate assumptions that have been made. It could for example allow for
 * an undeclared dependency between two build steps.
 *
 * Case folding: On some systems (such as most Mac OS X and Windows systems),
 * the path A.txt and a.txt point to the same file. Part of this problem is
 * solved by the (st_ino, st_dev) solution mentioned above. The part that is not
 * fixed is the part of the path that does not yet exist. For this, Shuriken
 * normalizes and case folds the path.
 *
 * On systems that have case sensitive file systems, this could cause extraenous
 * dependencies between targets, when there are different output files that have
 * the same path when case folding. To avoid this, Paths disallows creating Path
 * objects that are different but the same when case folded. This is
 * conservative but that is arguably a nice thing: It makes sure that the build
 * does not break when run on systems with different case sensitivity settings.
 *
 * ---
 *
 * A key aspect of Paths is that its comparison routines only work if all stat
 * calls it makes are done while the file system is not modified. In practice
 * this means that Path objects can be created during manifest parsing and when
 * Shuriken is calculating what to build, but once the build has started, it
 * is not possible to create new Path objects.
 */
class Paths {
 public:
  Paths(FileSystem &file_system);

  Path get(const std::string &path) throw(PathError);
  Path get(std::string &&path) throw(PathError);

 private:
  FileSystem &_file_system;
  std::unordered_set<detail::CanonicalizedPath> _canonicalized_paths;
  std::unordered_set<std::string> _original_paths;
};

}  // namespace shk
