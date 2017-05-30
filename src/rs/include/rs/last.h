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

#include <rs/map.h>
#include <rs/pipe.h>
#include <rs/reduce.h>

namespace shk {

/**
 * Takes a stream of values and returns the last of them. If there is no value,
 * the operator fails with an std::out_of_range exception.
 */
template <typename T>
auto Last() {
  return Pipe(
    ReduceGet(
        [] { return std::unique_ptr<T>(); },
        [](std::unique_ptr<T> &&accum, auto &&value) {
          return std::unique_ptr<T>(
              new T(std::forward<decltype(value)>(value)));
        }),
    Map([](std::unique_ptr<T> &&value) {
      if (value) {
        return std::move(*value);
      } else {
        throw std::out_of_range("Last invoked with empty stream");
      }
    }));
}

}  // namespace shk
