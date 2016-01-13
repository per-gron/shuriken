#pragma once

#include <stdexcept>
#include <string>

namespace shk {

class PathError : public std::runtime_error {
 public:
  template <typename string_type>
  explicit PathError(const string_type &what, const std::string &path)
      : runtime_error(what),
        _what(what),
        _path(path) {}

  virtual const char *what() const throw() {
    return _what.c_str();
  }

  const std::string &path() const {
    return _path;
  }

 private:
  const std::string _what;
  const std::string _path;
};

}  // namespace shk
