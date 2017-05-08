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

#include <util/path_operations.h>

namespace shk {

std::pair<nt_string_view, nt_string_view> basenameSplitPiece(nt_string_view path) {
  const auto last_nonslash = path.find_last_not_of('/');
  const auto slash_pos = path.find_last_of('/', last_nonslash);

  if (slash_pos == string_view::npos) {
    return std::make_pair(nt_string_view(".", 1), path);
  } else if (last_nonslash == string_view::npos) {
    return std::make_pair(nt_string_view("/", 1), nt_string_view("/", 1));
  } else {
    return std::make_pair(
        slash_pos == 0 ?
            nt_string_view("/", 1) :
            nt_string_view(path.data(), slash_pos),
        nt_string_view(
            path.data() + slash_pos + 1,
            last_nonslash - slash_pos));
  }
}

nt_string_view dirname(nt_string_view path) {
  return basenameSplitPiece(path).first;
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

}  // namespace shk
