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
  friend class Backreference<T>;
  template <typename U>
  friend Backreferee<typename std::decay<U>::type> WithBackreference(
      U &&, Backreference<typename std::decay<U>::type> *);

  template <typename U>
  explicit Backreferee(U &&value) : T(std::forward<U>(value)) {}

  Backreference<T> *backref_ = nullptr;
};

template <typename T>
class Backreference {
 public:
  Backreference() : val_(nullptr) {}

  ~Backreference() {
    if (val_) {
      val_->backref_ = nullptr;
    }
  }

  Backreference(const Backreference &) = delete;
  Backreference &operator=(const Backreference &) = delete;

  Backreference(Backreference &&other)
      : val_(other.val_) {
    other.val_ = nullptr;
    if (val_) {
      val_->backref_ = this;
    }
  }

  Backreference &operator=(Backreference &&other) {
    if (val_) {
      val_->backref_ = nullptr;
    }
    val_ = other.val_;
    other.val_ = nullptr;
    if (val_) {
      val_->backref_ = this;
    }
    return *this;
  }

  void Reset() {
    if (val_) {
      val_->backref_ = nullptr;
    }
    val_ = nullptr;
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
  friend class Backreferee<T>;
  template <typename U>
  friend Backreferee<typename std::decay<U>::type> WithBackreference(
      U &&, Backreference<typename std::decay<U>::type> *);

  explicit Backreference(Backreferee<T> *val) : val_(val) {}

  Backreferee<T> *val_;
};

template <typename T>
Backreferee<typename std::decay<T>::type> WithBackreference(
    T &&t, Backreference<typename std::decay<T>::type> *backref) {
  Backreferee<typename std::decay<T>::type> backreferee(std::forward<T>(t));
  *backref = Backreference<typename std::decay<T>::type>(&backreferee);
  return backreferee;
}

}  // namespace shk
