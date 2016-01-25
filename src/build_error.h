#pragma once

#include <stdexcept>
#include <string>

namespace shk {

class BuildError : public std::runtime_error {
 public:
  template <typename string_type>
  explicit BuildError(const string_type &what)
      : runtime_error(what),
        _what(what) {}

  virtual const char *what() const throw() {
    return _what.c_str();
  }

 private:
  const std::string _what;
};

}  // namespace shk
