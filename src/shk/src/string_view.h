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
