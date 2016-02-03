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

#include <string.h>

/**
 * StringPiece represents a slice of a string whose memory is managed
 * externally.  It is useful for reducing the number of std::strings
 * we need to allocate.
 */
class StringPiece {
 public:
  StringPiece() : _str(NULL), _len(0) {}

  /**
   * The constructors intentionally allow for implicit conversions.
   */
  StringPiece(const std::string &str) : _str(str.data()), _len(str.size()) {}
  StringPiece(const char *str) : _str(str), _len(strlen(str)) {}

  StringPiece(const char *str, size_t len) : _str(str), _len(len) {}

  bool operator==(const StringPiece &other) const {
    return _len == other._len && memcmp(_str, other._str, _len) == 0;
  }
  bool operator!=(const StringPiece &other) const {
    return !(*this == other);
  }

  /**
   * Convert the slice into a full-fledged std::string, copying the
   * data into a new string.
   */
  std::string asString() const {
    return _len ? std::string(_str, _len) : std::string();
  }

  const char *_str;
  size_t _len;
};
