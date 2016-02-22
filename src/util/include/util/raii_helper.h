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
      : obj_(obj) {}

  RAIIHelper(const RAIIHelper &) = delete;
  RAIIHelper &operator=(const RAIIHelper &) = delete;

  RAIIHelper(RAIIHelper &&other)
      : obj_(other.obj_) {
    other.obj_ = nullptr;
  }

  ~RAIIHelper() {
    if (Predicate(obj_)) {
      Free(obj_);
    }
  }

  T get() const {
    return obj_;
  }

 private:
  T obj_;
};

}  // namespace util
