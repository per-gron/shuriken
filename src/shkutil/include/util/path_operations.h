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

#pragma once

#include <string>

#include <util/path_error.h>
#include <util/string_view.h>

namespace shk {

/**
 * Split a path into its dirname and basename. The first element in the
 * pair is the dirname, the second the basename.
 *
 * Acts like the dirname and basename functions in the standard library.
 */
std::pair<nt_string_view, nt_string_view> basenameSplitPiece(
    nt_string_view path);

nt_string_view dirname(nt_string_view path);

/**
 * Canonicalize a path like "foo/../bar.h" into just "bar.h". This function does
 * not consult the file system so it will do the wrong thing if a directory
 * prior to a .. is a symlink and in similar situations that involve symlinks.
 */
void canonicalizePath(std::string *path) throw(PathError);
void canonicalizePath(char *path, size_t *len) throw(PathError);

}  // namespace shk
