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

#include <util/intrinsics.h>

#define SHK_ASSERT(condition)                        \
  do {                                               \
    if (!(condition)) {                              \
      ::shk::detail::assertionFailure(               \
          #condition,                                \
          __FILE__,                                  \
          __LINE__);                                 \
    }                                                \
  } while (0)

namespace shk {
namespace detail {

NO_RETURN void assertionFailure(
    const char *condition, const char *file, int line);

}  // namespace detail

/**
 * Log a fatal message and exit.
 */
NO_RETURN void fatal(const char *msg, ...);

/**
 * Log a warning message.
 */
void warning(const char *msg, ...);

/**
 * Log an error message.
 */
void error(const char *msg, ...);

}  // namespace shk
