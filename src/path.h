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
static void canonicalizePath(
    std::string *path,
    shk::SlashBits *slash_bits) throw(PathError);
static void canonicalizePath(
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
  Path(
      const detail::CanonicalizedPath *canonicalized_path,
      SlashBits slash_bits)
      : _canonicalized_path(canonicalized_path),
        _slash_bits(slash_bits) {}

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
      _canonicalized_path == other._canonicalized_path &&
      _slash_bits == other._slash_bits);
  }

 private:
  const detail::CanonicalizedPath *_canonicalized_path;
  SlashBits _slash_bits;
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

class Paths {
 public:
  Path get(const std::string &path) throw(PathError);

 private:
  std::unordered_set<detail::CanonicalizedPath> _canonicalized_paths;
};

}  // namespace shk
