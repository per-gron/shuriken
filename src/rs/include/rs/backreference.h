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

#include <type_traits>

namespace shk {

template <typename T>
class Backreference;

template <typename T>
class Backreferee : public T {
 public:
  ~Backreferee() {
    if (backref_) {
      backref_->val_ = nullptr;
    }
  }
  using T::operator=;

  Backreferee(const Backreferee &) = delete;
  Backreferee &operator=(const Backreferee &) = delete;

  Backreferee(Backreferee &&other)
      : T(std::move(static_cast<T &>(other))),
        backref_(other.backref_) {
    other.backref_ = nullptr;
    if (backref_) {
      backref_->val_ = this;
    }
  }

  Backreferee &operator=(Backreferee &&other) {
    if (backref_) {
      backref_->val_ = nullptr;
    }

    T::operator=(std::move(other));
    backref_ = other.backref_;
    other.backref_ = nullptr;
    if (backref_) {
      backref_->val_ = this;
    }
    return *this;
  }

 private:
  template <typename> friend class Backreference;
  template <typename U, typename V>
  friend Backreferee<typename std::decay<U>::type> WithBackreference(
      U &&, Backreference<V> *);

  template <typename U>
  explicit Backreferee(U &&value) : T(std::forward<U>(value)) {}

  Backreference<T> *backref_ = nullptr;
};

// This class is final because I'm not sure the reinterpret_cast below works
// otherwise.
template <typename T>
class Backreference final {
 public:
  Backreference()
      : val_(nullptr),
        set_backref_(nullptr) {}

  ~Backreference() {
    if (val_) {
      set_backref_(val_, nullptr);
    }
  }

  Backreference(const Backreference &) = delete;
  Backreference &operator=(const Backreference &) = delete;

  Backreference(Backreference &&other)
      : val_(other.val_),
        set_backref_(other.set_backref_) {
    other.val_ = nullptr;
    other.set_backref_ = nullptr;
    if (val_) {
      set_backref_(val_, this);
    }
  }

  Backreference &operator=(Backreference &&other) {
    if (val_) {
      set_backref_(val_, nullptr);
    }
    val_ = other.val_;
    set_backref_ = other.set_backref_;
    other.val_ = nullptr;
    other.set_backref_ = nullptr;
    if (val_) {
      set_backref_(val_, this);
    }
    return *this;
  }

  void Reset() {
    if (val_) {
      set_backref_(val_, nullptr);
    }
    val_ = nullptr;
    set_backref_ = nullptr;
  }

  explicit operator bool() const {
    return !!val_;
  }

  T &operator*() {
    return *val_;
  }

  const T &operator*() const {
    return *val_;
  }

  T *operator->() {
    return val_;
  }

  const T *operator->() const {
    return val_;
  }

 private:
  template <typename> friend class Backreferee;
  template <typename U, typename V>
  friend Backreferee<typename std::decay<U>::type> WithBackreference(
      U &&, Backreference<V> *);

  template <typename SubType, typename SuperType>
  static void SetBackref(SuperType *val, Backreference *ptr) {
    static_cast<Backreferee<SubType> *>(val)->backref_ =
        reinterpret_cast<Backreference<SubType> *>(ptr);
  }

  template <typename U>
  explicit Backreference(Backreferee<U> *val)
      : val_(val),
        set_backref_(&SetBackref<U, T>) {}

  T *val_;
  void (*set_backref_)(T *val, Backreference *ptr);
};

template <typename T, typename U>
Backreferee<typename std::decay<T>::type> WithBackreference(
    T &&t, Backreference<U> *backref) {
  Backreferee<typename std::decay<T>::type> backreferee(std::forward<T>(t));
  *backref = Backreference<U>(&backreferee);
  return backreferee;
}

}  // namespace shk
