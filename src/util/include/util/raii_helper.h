#pragma once

#include <utility>

namespace util {

template<typename T>
bool isNonZero(T val) {
  return val;
}

template<
    typename T,
    typename Return,
    Return (Free)(T),
    bool (Predicate)(T) = isNonZero>
class RAIIHelper {
 public:
  RAIIHelper(T obj)
      : _obj(obj) {}

  RAIIHelper(const RAIIHelper &) = delete;
  RAIIHelper &operator=(const RAIIHelper &) = delete;

  RAIIHelper(RAIIHelper &&other)
      : _obj(other._obj) {
    other._obj = nullptr;
  }

  ~RAIIHelper() {
    if (Predicate(_obj)) {
      Free(_obj);
    }
  }

  explicit operator bool() const {
    return Predicate(_obj);
  }

  T get() const {
    return _obj;
  }

 private:
  T _obj;
};

}  // namespace util
