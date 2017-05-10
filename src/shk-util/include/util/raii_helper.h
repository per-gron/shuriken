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

#include <utility>

namespace shk {

template<
    typename T,
    typename Return,
    Return (Free)(T),
    T EmptyValue = nullptr>
class RAIIHelper {
 public:
  RAIIHelper() : _obj(EmptyValue) {}

  explicit RAIIHelper(T obj)
      : _obj(obj) {}

  RAIIHelper(const RAIIHelper &) = delete;
  RAIIHelper &operator=(const RAIIHelper &) = delete;

  RAIIHelper(RAIIHelper &&other)
      : _obj(other._obj) {
    other._obj = EmptyValue;
  }

  ~RAIIHelper() {
    if (_obj != EmptyValue) {
      Free(_obj);
    }
  }

  explicit operator bool() const {
    return _obj != EmptyValue;
  }

  T get() const {
    return _obj;
  }

  T release() {
    T ret = EmptyValue;
    std::swap(ret, _obj);
    return ret;
  }

  void reset(T new_val = EmptyValue) {
    auto old = _obj;
    _obj = new_val;
    if (old != EmptyValue) {
      Free(old);
    }
  }

 private:
  T _obj;
};

}  // namespace shk
