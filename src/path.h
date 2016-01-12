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

namespace shk {

using SlashBits = uint64_t;

namespace detail {

/**
 * Canonicalize a path like "foo/../bar.h" into just "bar.h".
 * |slash_bits| has bits set starting from lowest for a backslash that was
 * normalized to a forward slash. (only used on Windows)
 */
static bool canonicalizePath(
    std::string *path,
    shk::SlashBits *slash_bits,
    std::string *err);
static bool canonicalizePath(
    char *path,
    size_t *len,
    shk::SlashBits *slash_bits,
    std::string *err);

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
}  // namespace shk

namespace std
{

template<>
struct hash<shk::detail::CanonicalizedPath> {
  using argument_type = shk::detail::CanonicalizedPath;
  using result_type = std::size_t;

  result_type operator()(const argument_type &p) const {
    return std::hash<std::string>()(p.path);
  }
};

}

namespace shk {

class Paths;

class Path {
 public:
  Path(
      const detail::CanonicalizedPath *canonicalized_path,
      SlashBits slash_bits)
      : _canonicalized_path(canonicalized_path),
        _slash_bits(slash_bits) {}

  /**
   * Returns true if the paths point to the same paths. operator== is not
   * used because this does not take slash_bits into account.
   */
  bool isSame(const Path &other) const {
    return _canonicalized_path == other._canonicalized_path;
  }

  std::string pathDecanonicalized() const;

 private:
  const detail::CanonicalizedPath *_canonicalized_path;
  SlashBits _slash_bits;
};

class Paths {
 public:
  Path get(const std::string &path);

 private:
  std::unordered_set<detail::CanonicalizedPath> _canonicalized_paths;
};

}  // namespace shk
