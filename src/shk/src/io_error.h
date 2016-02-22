#pragma once

#include <stdexcept>
#include <string>

namespace shk {

class IoError : public std::runtime_error {
 public:
  template <typename string_type>
  explicit IoError(const string_type &what, int code)
      : runtime_error(what),
        code(code),
        _what(what) {}

  virtual const char *what() const throw() {
    return _what.c_str();
  }

  const int code;

 private:
  const std::string _what;
};

}  // namespace shk
