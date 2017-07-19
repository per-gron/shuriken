// Copyright 2011 Google Inc. All Rights Reserved.
// Copyright 2017 Per Grön. All Rights Reserved.
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

#include "fs/path.h"

#include <errno.h>
#include <sys/stat.h>

#include <util/path_operations.h>

namespace shk {
namespace {

#ifdef _WIN32
template<typename Iter>
void replaceBackslashes(const Iter begin, const Iter end) {
  for (auto c = begin; c < end; ++c) {
    if (*c == '\\') {
      *c = '/';
    }
  }
}
#endif

/**
 * Helper class for doing stat calls in a memoized fashion.
 */
class Stater {
 public:
  Stater(detail::StatMemo &memo, FileSystem &file_system)
      : _memo(memo),
        _file_system(file_system) {}

  Stat stat(string_view path) {
    return stat(std::string(path), _memo.stat, &FileSystem::stat);
  }

  Stat lstat(string_view path) {
    return stat(std::string(path), _memo.lstat, &FileSystem::lstat);
  }

 private:
  Stat stat(
      const std::string &path,
      std::unordered_map<std::string, Stat> &memo,
      Stat (FileSystem::*stat_fn)(nt_string_view)) {
    const auto it = memo.find(path);
    if (it == memo.end()) {
      Stat stat = (_file_system.*stat_fn)(path);
      memo.emplace(path, stat);
      return stat;
    } else {
      return it->second;
    }
  }

  detail::StatMemo &_memo;
  FileSystem &_file_system;
};

detail::CanonicalizedPath makeCanonicalizedPath(
    Stater stater,
    std::string &&path) throw(PathError) {
  if (path.empty()) {
    throw PathError("Empty path", path);
  }

#ifdef _WIN32
  replaceBackslashes(path.begin(), path.end());
#endif

  // We have a path (say /a/b/c) and want to find a prefix of this path that
  // exists on the file system (for example /a).
  //
  // pos points to the last character in the path that is about to be tested for
  // existence.
  auto pos = path.size() - 1;  // The string is verified to not be empty above
  Stat stat;
  bool at_root = false;
  bool at_relative_root = false;
  for (;;) {
    // Use lstat only for the final component in a path. A build step's output
    // is allowed be a symlink to another build step's output.
    //
    // Other than that final component, the idea is to follow symlinks to the
    // actual file to directory where this will live. Comparing links for
    // identity does no good.
    //
    // Because paths to directories can end with slashes, this check needs to
    // be done before we overwrite the pos variable below.
    const bool use_lstat = pos == path.size() - 1;

    // Discard any trailing slashes. They have no semantic meaning.
    while (path[pos] == '/') {
      if (pos == 0) {
        // As a special case, don't discard a trailing slash if the path is only
        // "/", since that would transform an absolute path into a relative one.
        at_root = true;
        break;
      }
      pos--;
    }

    const auto path_to_try = string_view(
        at_relative_root ? "." : path.c_str(),
        pos + 1);
    stat = use_lstat ? stater.lstat(path_to_try) : stater.stat(path_to_try);

    if (stat.result == 0) {
      // Found an existing file or directory
      if (pos != path.size() - 1 && !S_ISDIR(stat.metadata.mode)) {
        // This is not the final path component (or there are slashes after the
        // actual path name), so it has to be a directory.
        throw PathError(
            "Encountered file in a directory component of a path", path);
      }
      break;
    } else if (at_root || at_relative_root) {
      throw PathError(
          "None of the path components can be accessed and exist", path);
    } else {
      while (path[pos] != '/') {
        if (pos == 0) {
          // The loop hit the beginning of the string. That means this is a
          // relative path and this none of the path components other than the
          // current working directory exist.
          at_relative_root = true;
          break;
        }
        pos--;
      }
    }
  }

  // At this point, the longest prefix of path that actually exists has been
  // found. Now extract the nonexisting part of the path and canonicalize it.
  if (!at_relative_root) {
    do {
      pos++;
    } while (pos != path.size() && path[pos] == '/');
  }
  auto len = path.size() - pos;
  std::string nonexisting_part(&path[pos], len);
  if (len > 0) {
    canonicalizePath(&nonexisting_part[0], &len);
    nonexisting_part.resize(len);
  }

  return detail::CanonicalizedPath(
      stat.metadata.ino,
      stat.metadata.dev,
      std::move(nonexisting_part));
}

}  // anonymous namespace

bool Path::exists() const {
  return _canonicalized_path->path.empty();
}

detail::Optional<FileId> Path::fileId() const {
  if (exists()) {
    return detail::Optional<FileId>(
        FileId(_canonicalized_path->ino, _canonicalized_path->dev));
  } else {
    return detail::Optional<FileId>();
  }
}

Paths::Paths(FileSystem &file_system)
    : _file_system(file_system) {}

Path Paths::get(const std::string &path) throw(PathError) {
  return get(std::string(path));
}

Path Paths::get(std::string &&path) throw(PathError) {
  auto stater = Stater(_stat_memo, _file_system);
  const auto original_result = _original_paths.emplace(path);
  const auto canonicalized_result = _canonicalized_paths.insert(
      makeCanonicalizedPath(stater, std::move(path)));
  return Path(
      &*canonicalized_result.first,
      &*original_result.first);
}

}  // namespace shk
