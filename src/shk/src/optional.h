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
#include <new>
#include <algorithm>

namespace shk {

namespace internal {

template<typename T, bool IsReference>
struct OptionalTypes {
};

template<typename T>
struct OptionalTypes<T, false> {
  typedef T        value_type;
  typedef T        storage_type;
  typedef T const &reference_const_type;
  typedef T       &reference_type;
  typedef T const *pointer_const_type;
  typedef T       *pointer_type;
  typedef T const &argument_type;
};

template<typename T>
struct OptionalTypes<T, true> {
  typedef typename std::remove_reference<T>::type value_type;
  typedef value_type *storage_type;
  typedef value_type  const &reference_const_type;
  typedef value_type &reference_type;
  typedef value_type  const *pointer_const_type;
  typedef value_type *pointer_type;
  typedef value_type &argument_type;
};

}  // namespace internal


/**
 * Optional is a class that encapsulates the concept of an optional value. It
 * does not allocate memory, but stores the value "by value", plus one byte for
 * keeping track of whether the value is set or not.
 *
 * Using this class is not unlike using a smart pointer; it overloads the * and
 * -> operators.
 *
 * It also has a couple of higher-level functional-like constructs for dealing
 * with the fact that the value might not be set: See map, each and ifelse.
 */
template<typename T>
class Optional {
 public:
  typedef internal::OptionalTypes<T, std::is_reference<T>::value> types;
  typedef typename types::value_type value_type;
  typedef typename types::storage_type storage_type;
  typedef typename types::reference_const_type reference_const_type;
  typedef typename types::reference_type reference_type;
  typedef typename types::pointer_const_type pointer_const_type;
  typedef typename types::pointer_type pointer_type;
  typedef typename types::argument_type argument_type;

  Optional() { setSet(false); }
  Optional(const Optional<T> &other) {
    setSet(false);
    if (other) {
      assign(*other.memory());
    }
  }
  Optional(Optional<T> &&other) {
    setSet(false);
    if (other) {
      assign(std::move(*other.memory()));
      other.clear();
    }
  }
  explicit Optional(argument_type other) {
    setSet(false);
    assign(other);
  }
  explicit Optional(value_type &&other) {
    setSet(false);
    assign(std::move(other));
  }

  ~Optional() { clear(); }

  Optional<T>& operator=(const Optional<T> &other) {
    if (other) {
      assign(*other.memory());
    } else {
      clear();
    }
    return *this;
  }

  Optional<T>& operator=(Optional<T> &&other) {
    if (other) {
      assign(std::move(*other.memory()));
      other.clear();
    } else {
      clear();
    }
    return *this;
  }

  Optional<T>& operator=(argument_type other) {
    assign(other);
    return *this;
  }

  Optional<T>& operator=(value_type &&other) {
    assign(std::move(other));
    return *this;
  }

  /**
   * Returns non-zero iff the object stores a value.
   */
  bool isSet() const {
    return _data[sizeof(storage_type)];
  }

  /**
   * Bool conversion operator, for use in if statements etc.
   * This is an alias for isSet.
   */
  explicit operator bool() const {
    return isSet();
  }

  /**
   * Returns a pointer to the object stored, or nullptr if not set.
   */
  pointer_type get() { return isSet() ? memory() : nullptr; }
  pointer_const_type get() const { return isSet() ? memory() : nullptr; }

  /**
   * This is a convenience operator in order to be able to use the
   * object as you would a smart pointer. Returns nullptr if not set.
   */
  pointer_type operator->() { return get(); }
  pointer_const_type operator->() const { return get(); }

  /**
   * This is a convenience operator in order to be able to use the
   * object as you would a smart pointer. Return value is undefined
   * if not set.
   */
  reference_type operator*() { return *get(); }
  reference_const_type operator*() const { return *get(); }

  /**
   * If the object stores a value, clear it. If the
   * object is not set, this is a no-op.
   */
  void clear() {
    if (isSet()) {
      invokeDestructor();
      setSet(false);
    }
  }

  /**
   * Takes a functor and invokes it with the object, and returns an optional of
   * the return value of the functor, if set. If not set, returns an empty
   * optional of the functor's return type.
   */
  template<typename Functor>
  auto map(Functor functor)
  -> Optional<decltype(functor(*static_cast<pointer_type>(nullptr)))> {
    typedef decltype(functor(*static_cast<pointer_type>(nullptr))) ReturnType;
    if (isSet()) {
      return Optional<ReturnType>(functor(*get()));
    } else {
      return Optional<ReturnType>();
    }
  }

  /**
   * Takes a functor and invokes it with the object if set. Otherwise, this
   * is a no-op.
   */
  template<typename Functor>
  void each(Functor functor) {
    if (isSet()) {
      functor(*get());
    }
  }

  /**
   * Takes a functor and invokes it with the object if set. Otherwise, this
   * is a no-op.
   */
  template<typename Functor>
  void each(Functor functor) const {
    if (isSet()) {
      functor(*get());
    }
  }

  /**
   * Takes two functors. The first one is called if the object is set, the
   * second one if it isn't. It returns what the invoked functor returns.
   */
  template<typename FunctorIf, typename FunctorElse>
  auto ifElse(FunctorIf functorIf, FunctorElse functorElse)
  -> decltype(functorIf(*static_cast<pointer_type>(nullptr))) {
    if (isSet()) {
      return functorIf(*get());
    } else {
      return functorElse();
    }
  }

 private:
  template<typename U = T>
  typename std::enable_if<std::is_reference<U>::value>::type
  invokeDestructor() {}

  template<typename U = T>
  typename std::enable_if<!std::is_reference<U>::value>::type
  invokeDestructor() {
    memory()->~T();
  }

  template<typename U = T>
  typename std::enable_if<std::is_reference<U>::value, pointer_type>::type
  memory() {
    return *reinterpret_cast<value_type**>(_data);
  }

  template<typename U = T>
  typename std::enable_if<!std::is_reference<U>::value,
                          pointer_type>::type
  memory() {
    return reinterpret_cast<pointer_type>(&_data);
  }

  template<typename U = T>
  typename std::enable_if<std::is_reference<U>::value,
                          pointer_const_type>::type
  memory() const {
    return *reinterpret_cast<value_type* const*>(_data);
  }

  template<typename U = T>
  typename std::enable_if<!std::is_reference<U>::value,
                          pointer_const_type>::type
  memory() const {
    return reinterpret_cast<pointer_const_type>(&_data);
  }

  void setSet(bool set) {
    _data[sizeof(storage_type)] = set;
  }

  template<typename U = T>
  typename std::enable_if<std::is_reference<U>::value>::type
  copyConstruct(argument_type val) {
    *reinterpret_cast<value_type**>(_data) = &val;
  }

  template<typename U = T>
  typename std::enable_if<!std::is_reference<U>::value>::type
  copyConstruct(argument_type val) {
    ::new(memory()) T(val);
  }

  template<typename U = T>
  typename std::enable_if<std::is_reference<U>::value>::type
  moveConstruct(value_type&& val) {
    *reinterpret_cast<value_type**>(_data) = &val;
  }

  template<typename U = T>
  typename std::enable_if<!std::is_reference<U>::value>::type
  moveConstruct(value_type&& val) {
    ::new(memory()) T(std::move(val));
  }

  template<typename U = T>
  typename std::enable_if<std::is_copy_constructible<U>::value &&
                          std::is_copy_assignable<U>::value>::type
  assign(argument_type by_copy) {
    if (isSet()) {
      *memory() = by_copy;
    } else {
      copyConstruct(by_copy);
      setSet(true);
    }
  }

  template<typename U = T>
  typename std::enable_if<std::is_copy_constructible<U>::value &&
                          !std::is_copy_assignable<U>::value>::type
  assign(argument_type by_copy) {
    if (isSet()) {
      clear();
    }
    copyConstruct(by_copy);
    setSet(true);
  }

  template<typename U = T>
  typename std::enable_if<!std::is_copy_constructible<U>::value &&
                          std::is_copy_assignable<U>::value>::type
  assign(argument_type by_copy) {
    if (!isSet()) {
      ::new(memory()) T;
      setSet(true);
    }
    *memory() = by_copy;
  }

  template<typename U = T>
  typename std::enable_if<std::is_move_constructible<U>::value &&
                          std::is_move_assignable<U>::value>::type
  assign(value_type &&by_move) {
    if (isSet()) {
      *memory() = std::move(by_move);
    } else {
      moveConstruct(std::move(by_move));
      setSet(true);
    }
  }

  template<typename U = T>
  typename std::enable_if<std::is_move_constructible<U>::value &&
                          !std::is_move_assignable<U>::value>::type
  assign(value_type &&by_move) {
    if (isSet()) {
      clear();
    }
    moveConstruct(std::move(by_move));
    setSet(true);
  }

  template<typename U = T>
  typename std::enable_if<!std::is_move_constructible<U>::value &&
                          std::is_move_assignable<U>::value>::type
  assign(value_type &&by_move) {
    if (!isSet()) {
      ::new(memory()) T;
      setSet(true);
    }
    *memory() = std::move(by_move);
  }

  /**
   * The last byte of this char array is non-zero if
   * _data contains an initialized value.
   */
  alignas(T) char _data[sizeof(storage_type)+1];
};


// Optional vs Optional

template<typename T>
bool operator==(const Optional<T> &x, const Optional<T> &y) {
  return ((!x && !y) ||
          (x && y && *x == *y));
}

template<typename T>
bool operator<(const Optional<T> &x, const Optional<T> &y) {
  if (!y) {
    return false;
  } else if (!x) {
    return true;
  } else {
    return (*x < *y);
  }
}

template<typename T>
bool operator!=(const Optional<T> &x, const Optional<T> &y) {
  return !(x == y);
}

template<typename T>
bool operator>(const Optional<T> &x, const Optional<T> &y) {
  return y < x;
}

template<typename T>
bool operator<=(const Optional<T> &x, const Optional<T> &y) {
  return !(x > y);
}

template<typename T>
bool operator>=(const Optional<T> &x, const Optional<T> &y) {
  return !(x < y);
}


// T vs Optional

template<typename T>
bool operator==(const typename std::remove_reference<T>::type &x,
                const Optional<T> &y) {
  return (y && x == *y);
}

template<typename T>
bool operator<(const typename std::remove_reference<T>::type &x,
               const Optional<T> &y) {
  if (!y) {
    return false;
  } else {
    return (x < *y);
  }
}

template<typename T>
bool operator!=(const typename std::remove_reference<T>::type &x,
                const Optional<T> &y) {
  return !(x == y);
}

template<typename T>
bool operator>(const typename std::remove_reference<T>::type &x,
               const Optional<T> &y) {
  return y < x;
}

template<typename T>
bool operator<=(const typename std::remove_reference<T>::type &x,
                const Optional<T> &y) {
  return !(x > y);
}

template<typename T>
bool operator>=(const typename std::remove_reference<T>::type &x,
                const Optional<T> &y) {
  return !(x < y);
}


// Optional vs T

template<typename T>
bool operator==(const Optional<T> &x,
                const typename std::remove_reference<T>::type &y) {
  return (x && *x == y);
}

template<typename T>
bool operator<(const Optional<T> &x,
               const typename std::remove_reference<T>::type &y) {
  if (!x) {
    return true;
  } else {
    return (*x < y);
  }
}

template<typename T>
bool operator!=(const Optional<T> &x,
                const typename std::remove_reference<T>::type &y) {
  return !(x == y);
}

template<typename T>
bool operator>(const Optional<T> &x,
               const typename std::remove_reference<T>::type &y) {
  return y < x;
}

template<typename T>
bool operator<=(const Optional<T> &x,
                const typename std::remove_reference<T>::type &y) {
  return !(x > y);
}

template<typename T>
bool operator>=(const Optional<T> &x,
                const typename std::remove_reference<T>::type &y) {
  return !(x < y);
}

}  // namespace shk


namespace std {

template<typename T>
void swap(shk::Optional<T> &lhs, shk::Optional<T> &rhs) {
  if (!lhs && !rhs) {
    // No-op
  } else if (!rhs) {
    rhs = std::move(lhs);
  } else if (!lhs) {
    lhs = std::move(rhs);
  } else {
    std::swap(*lhs, *rhs);
  }
}

}  // namespace std
