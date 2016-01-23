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

#include "path.h"

#include <sys/stat.h>

namespace shk {
namespace detail {

std::pair<StringPiece, StringPiece> basenameSplitPiece(
    const std::string &path) {
  const auto last_nonslash = path.find_last_not_of('/');
  const auto slash_pos = path.find_last_of('/', last_nonslash);

  if (slash_pos == std::string::npos) {
    return std::make_pair(StringPiece(".", 1), StringPiece(path));
  } else if (last_nonslash == std::string::npos) {
    return std::make_pair(StringPiece("/", 1), StringPiece("/", 1));
  } else {
    return std::make_pair(
        slash_pos == 0 ?
            StringPiece("/", 1) :
            StringPiece(path.data(), slash_pos),
        StringPiece(
            path.data() + slash_pos + 1,
            last_nonslash - slash_pos));
  }
}

void canonicalizePath(std::string *path) throw(PathError) {
  size_t len = path->size();
  char *str = 0;
  if (len > 0) {
    str = &(*path)[0];
    canonicalizePath(str, &len);
    path->resize(len);
  }
  if (len == 0) {
    *path = ".";
  }
}

void canonicalizePath(
    char *path,
    size_t *len) throw(PathError) {
  // WARNING: this function is performance-critical; please benchmark
  // any changes you make to it.
  if (*len == 0) {
    return;
  }

  const int kMaxPathComponents = 62;
  char *components[kMaxPathComponents];
  int component_count = 0;

  char *start = path;
  char *dst = start;
  const char *src = start;
  const char *end = start + *len;

  if (*src == '/') {
#ifdef _WIN32
    // network path starts with //
    if (*len > 1 && *(src + 1) == '/') {
      src += 2;
      dst += 2;
    } else {
      ++src;
      ++dst;
    }
#else
    ++src;
    ++dst;
#endif
  }

  while (src < end) {
    if (*src == '.') {
      if (src + 1 == end || src[1] == '/') {
        // '.' component; eliminate.
        src += 2;
        continue;
      } else if (src[1] == '.' && (src + 2 == end || src[2] == '/')) {
        // '..' component.  Back up if possible.
        if (component_count > 0) {
          dst = components[component_count - 1];
          src += 3;
          --component_count;
        } else {
          *dst++ = *src++;
          *dst++ = *src++;
          *dst++ = *src++;
        }
        continue;
      }
    }

    if (*src == '/') {
      src++;
      continue;
    }
 
    if (component_count == kMaxPathComponents) {
      throw PathError("path has too many components", path);
    }
    components[component_count] = dst;
    ++component_count;

    while (*src != '/' && src != end) {
      *dst++ = *src++;
    }
    *dst++ = *src++;  // Copy '/' or final \0 character as well.
  }

  if (dst == start) {
    *len = 0;
  } else {
    *len = dst - start - (component_count ? 1 : 0);
  }
}

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

CanonicalizedPath makeCanonicalizedPath(
    FileSystem &file_system, std::string &&path) {
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

    StringPiece path_to_try(
        at_relative_root ? "." : path.c_str(),
        pos + 1);
    // Use stat, not lstat: the idea is to follow symlinks to the actual file to
    // directory where this will live. Comparing links for identity does no
    // good.
    stat = file_system.stat(path_to_try.asString());

    if (stat.result == 0) {
      // Found an existing file or directory
      if (pos != path.size() - 1 && !S_ISDIR(stat.metadata.mode)) {
        // This is not the final path component, so it has to be a directory.
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
  do {
    pos++;
  } while (pos != path.size() && path[pos] == '/');
  auto len = path.size() - pos;
  std::string nonexisting_part(&path[pos], len);
  if (len > 0) {
    canonicalizePath(&nonexisting_part[0], &len);
    nonexisting_part.resize(len);
  }

  return CanonicalizedPath(
      stat.metadata.ino,
      stat.metadata.dev,
      std::move(nonexisting_part));
}

}  // anonymous namespace
}  // namespace detail

Paths::Paths(FileSystem &file_system)
    : _file_system(file_system) {}

Path Paths::get(const std::string &path) throw(PathError) {
  return get(std::string(path));
}

Path Paths::get(std::string &&path) throw(PathError) {
  const auto original_result = _original_paths.emplace(path);
  const auto canonicalized_result = _canonicalized_paths.insert(
      detail::makeCanonicalizedPath(_file_system, std::move(path)));
  return Path(
      &*canonicalized_result.first,
      &*original_result.first);
}

}  // namespace shk
