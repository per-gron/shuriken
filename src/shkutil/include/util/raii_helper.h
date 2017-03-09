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

 private:
  T _obj;
};

}  // namespace shk
