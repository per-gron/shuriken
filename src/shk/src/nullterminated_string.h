#pragma once

#include <algorithm>
#include <memory>

#include "string_view.h"

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
  BasicNullterminatedString(string_view view)
      : _heap(view.size() >= StackSpace ?
            new char[view.size() + 1] :
            nullptr) {
    char *ptr = _heap ? _heap.get() : _stack;
    memcpy(ptr, view.data(), view.size());
    ptr[view.size()] = 0;
  }

  const char *c_str() const {
    return _heap ? _heap.get() : _stack;
  }

 private:
  char _stack[StackSpace];
  std::unique_ptr<char[]> _heap;
};

using NullterminatedString = BasicNullterminatedString<1024>;

}  // namespace shk
