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

#include <algorithm>
#include <memory>

#include <util/string_view.h>

namespace shk {

/**
 * Helper for converting a string_view to a null terminated string. It copies
 * the string but tries to allocate stack space for doing it.
 *
 * This should not be used too much but it is useful where null terminated
 * strings are absolutely required, such as when making syscalls.
 */
template<size_t StackSpace = 1024>
class BasicNullterminatedString {
 public:
  explicit BasicNullterminatedString(string_view view)
      : _raw(nullptr),
        _heap(view.size() >= StackSpace ?
            new char[view.size() + 1] :
            nullptr) {
    char *ptr = _heap ? _heap.get() : _stack;
    memcpy(ptr, view.data(), view.size());
    ptr[view.size()] = 0;
  }

  explicit BasicNullterminatedString(nt_string_view view)
      : _raw(view.null_terminated() ? view.data() : nullptr),
        _heap(!_raw && view.size() >= StackSpace ?
            new char[view.size() + 1] :
            nullptr) {
    if (!_raw) {
      char *ptr = _heap ? _heap.get() : _stack;
      memcpy(ptr, view.data(), view.size());
      ptr[view.size()] = 0;
    }
  }

  const char *c_str() const {
    return _raw ? _raw : _heap ? _heap.get() : _stack;
  }

 private:
  char _stack[StackSpace];
  const char *_raw;
  std::unique_ptr<char[]> _heap;
};

using NullterminatedString = BasicNullterminatedString<1024>;

}  // namespace shk
