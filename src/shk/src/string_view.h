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

#include <experimental/string_view>

namespace shk {

using string_view = std::experimental::string_view;

/**
 * nt_string_view is a string_view, but it is supposed to be used only with
 * underlying memory chunks that are null terminated. The extra guarantee that
 * this provides is that it is possible to do sv[sv.size()] == '\0' to see if
 * no copy is needed to get a null terminated string out of it.
 *
 * That this class public inherits from string_view means that every
 * nt_string_view is a string_view, but not the other way around.
 */
class nt_string_view : public string_view {
 public:
  nt_string_view() noexcept
      : string_view("") {}

  // intentionally not explicit
  nt_string_view(const char *str) noexcept
      : string_view(str) {}

  nt_string_view(const char *str, size_t size) noexcept
      : string_view(str, size) {}

  // intentionally not explicit
  nt_string_view(const std::string &str) noexcept
      : string_view(str) {}

  bool null_terminated() const noexcept {
    return data()[size()] == '\0';
  }
};

}  // namespace shk

namespace std {

template<>
struct hash<shk::nt_string_view> {
  using argument_type = shk::nt_string_view;
  using result_type = std::size_t;

  result_type operator()(const argument_type &h) const {
    return hash<std::experimental::string_view>()(h);
  }
};

}  // namespace std
