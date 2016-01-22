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

#include "path_error.h"

namespace shk {

using SlashBits = uint64_t;

namespace detail {

/**
 * Canonicalize a path like "foo/../bar.h" into just "bar.h".
 * |slash_bits| has bits set starting from lowest for a backslash that was
 * normalized to a forward slash. (only used on Windows)
 */
void canonicalizePath(
    std::string *path,
    shk::SlashBits *slash_bits) throw(PathError);
void canonicalizePath(
    char *path,
    size_t *len,
    shk::SlashBits *slash_bits) throw(PathError);

struct CanonicalizedPath {
  explicit CanonicalizedPath(const std::string &path)
    : path(path) {}
  CanonicalizedPath(const CanonicalizedPath &) = delete;
  CanonicalizedPath &operator=(const CanonicalizedPath &) = delete;

  std::string path;
};

inline bool operator==(const CanonicalizedPath &a, const CanonicalizedPath &b) {
  return a.path == b.path;
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
  Path()
      : _canonicalized_path(nullptr)
#ifdef _WIN32
        , _slash_bits(0)
#endif
        {}

  Path(
      const detail::CanonicalizedPath *canonicalized_path,
      SlashBits slash_bits)
      : _canonicalized_path(canonicalized_path)
#ifdef _WIN32
        , _slash_bits(slash_bits)
#endif
        {}

  /**
   * Returns true if the paths point to the same paths. operator== is not
   * used for this because this does not take slash_bits into account.
   */
  bool isSame(const Path &other) const {
    return _canonicalized_path == other._canonicalized_path;
  }

  std::string decanonicalized() const;

  const std::string &canonicalized() const;

  bool operator==(const Path &other) const {
    return (
      _canonicalized_path == other._canonicalized_path
#ifdef _WIN32
      && _slash_bits == other._slash_bits
#endif
      );
  }

  bool operator!=(const Path &other) const {
    return !(*this == other);
  }

  /**
   * Comparison operator, suitable for use in maps and sets but the
   * comparison is dependent on memory layout so is not stable across runs.
   */
  bool operator<(const Path &other) const {
#ifdef _WIN32
    return (
        std::tie(_canonicalized_path, _slash_bits) <
        std::tie(other._canonicalized_path, other._slash_bits));
#else
    return _canonicalized_path < other._canonicalized_path;
#endif
  }

 private:
  const detail::CanonicalizedPath *_canonicalized_path;
#ifdef _WIN32
  SlashBits _slash_bits;
#endif
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
 * to paths contained in the manifest point to the same underlying file. Doing
 * this in an efficient way is a little bit tricky. This class solves this
 * problem.
 *
 * One part of the problem is that it is necessary to be able to compare two
 * paths efficiently. This is solved by providing a special Path class instead
 * of using plain strings. Path objects are interned, meaning that instead of
 * containing the path directly, it has a pointer to an object with the path.
 * Paths guarantees that these objects are unique, so to compare a path it is
 * sufficient to compare the pointer in the Path object.
 *
 * The other half of the problem is that it is necessary to ensure that there
 * can actually only exist one CanonicalizedPath (the type that Path objects
 * have a pointer to) objects are really unique for any given file. There are
 * several aspects to this:
 *
 * Path canonicalization: The path a/../c.txt and the path c.txt actually point
 * to the same file. In order to make sure that there is only one
 * CanonicalizedPath for both of these paths, paths are canonicalized before
 * they are put into a CanonicalizedPath object. Shuriken always canonicalizes
 * every path in the manifest before it does anything else with it.
 *
 * Links (symbolic and hard): If there is a symbolic link from a to b, the paths
 * a/c.txt and b/c.txt point to the same file. This problem is a little bit
 * harder to solve than the path canonicalization above, because it requires
 * consulting the file system. What Paths does here is to stat all paths and
 * record the st_ino and st_dev fields of the stat result. If st_ino and st_dev
 * are the same, then the paths are known to point to the same file, even if
 * links are playing tricks with us.
 *
 * Using st_ino and st_dev alone is not sufficient, though, because the manifest
 * often (always in the case of a clean build) contain paths to files that do
 * not yet exist. When Paths encounters a path that does not exist, it gets the
 * dirname of the path and tries to stat that, over and over again until it
 * finds an existing path. It then stores the st_ino and st_dev fields along
 * with the path after it. This is what a CanonicalizedPath object contains.
 *
 * For example, for the path /a/b/c/d/../e.txt, if /a/b exists but not /a/b/c,
 * then the CanonicalizedPath would be the st_ino and st_dev of /a/b combined
 * with c/e.txt.
 *
 * Absolute vs relative paths: If the current working directory is /a, then the
 * paths b.txt and /a/b.txt point to the same file. Solving this problem
 * requires system calls (at the very least to find the current working
 * directory). One way to solve it could be to absolutize all relative paths by
 * prepending the working directory. Shuriken does not do this though, because
 * the solution mentioned above to deal with links solves this issue too.
 *
 * Case folding: On some systems (such as most Mac OS X and Windows systems),
 * the path A.txt and a.txt point to the same file. Part of this problem is
 * solved by the (st_ino, st_dev) solution mentioned above. The part that is not
 * fixes is the part of the path that does not yet exist. For this, Shuriken
 * normalizes and case folds the path.
 *
 * On systems that have case sensitive file systems, this could cause extraenous
 * dependencies between targets, when there are different output files that have
 * the same path when case folding. To avoid this, Shuriken disallows build
 * steps that declare different outputs that case folds to the same thing. This
 * is conservative but it is arguably a nice thing: It makes sure that the build
 * does not break when run on systems with different case sensitivity.
 */
class Paths {
 public:
  Path get(const std::string &path) throw(PathError);
  Path get(std::string &&path) throw(PathError);

 private:
  std::unordered_set<detail::CanonicalizedPath> _canonicalized_paths;
};

}  // namespace shk
