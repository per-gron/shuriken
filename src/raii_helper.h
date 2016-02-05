#pragma once

#include <utility>

namespace shk {

template<typename T, typename Return, Return (Free)(T *)>
class RAIIHelper {
 public:
  RAIIHelper(T *obj)
      : obj_(obj) {}

  RAIIHelper(const RAIIHelper &) = delete;
  RAIIHelper &operator=(const RAIIHelper &) = delete;

  RAIIHelper(RAIIHelper &&other)
      : obj_(other.obj_) {
    other.obj_ = nullptr;
  }

  ~RAIIHelper() {
    if (obj_) {
      Free(obj_);
    }
  }

  T *get() const {
    return obj_;
  }

 private:
  T * obj_;
};

}  // namespace
