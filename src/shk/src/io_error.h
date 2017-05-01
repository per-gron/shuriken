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

#include <stdexcept>
#include <string>

#include "util.h"

namespace shk {

/**
 * IoError object represent either the success of an operation or a failure,
 * along with an error message and errno-style error code.
 */
class IoError {
 public:
  /**
   * Construct an IoError object that indicates that there is no error. The bool
   * conversion operator returns false for these objects.
   */
  IoError() = default;

  template <typename string_type>
  explicit IoError(const string_type &what, int code)
      : code(code),
        _what(what) {
    if (_what.empty()) {
      fatal("can't create IoError without error message");
    }
  }

  /**
   * Alias for IoError(), can be used for code clarity.
   */
  static IoError success() {
    return IoError();
  }

  virtual const char *what() const throw() {
    return _what.c_str();
  }

  /**
   * Returns true if the object represents a failure.
   */
  explicit operator bool() const {
    return !_what.empty();
  }

  bool operator==(const IoError &other) const {
    return
        code == other.code &&
        _what == other._what;
  }

  bool operator!=(const IoError &other) const {
    return !(*this == other);
  }

  int code = 0;

 private:
  // Empty string indicates no error
  std::string _what;
};

}  // namespace shk
