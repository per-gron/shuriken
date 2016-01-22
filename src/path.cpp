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

namespace shk {

namespace detail {

void canonicalizePath(std::string *path) throw(PathError) {
  size_t len = path->size();
  char* str = 0;
  if (len > 0) {
    str = &(*path)[0];
    canonicalizePath(str, &len);
    path->resize(len);
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
  char* components[kMaxPathComponents];
  int component_count = 0;

  char* start = path;
  char* dst = start;
  const char* src = start;
  const char* end = start + *len;

#ifdef _WIN32
  // Convert \ to /, setting a bit in |bits| for each \ encountered.
  for (char* c = path; c < end; ++c) {
    if (*c) {
      *c = '/';
    }
  }
#endif

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
    *len = dst - start - 1;
  }
}

}  // namespace detail

Path Paths::get(const std::string &path) throw(PathError) {
  return get(std::string(path));
}

Path Paths::get(std::string &&path) throw(PathError) {
  const auto original_result = _original_paths.emplace(path);
  detail::canonicalizePath(&path);
  const auto canonicalized_result = _canonicalized_paths.emplace(path);
  return Path(
      &*canonicalized_result.first,
      &*original_result.first);
}

}  // namespace shk
