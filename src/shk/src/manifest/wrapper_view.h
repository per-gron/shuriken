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

#include <utility>
#include <type_traits>

namespace shk {
namespace detail {

template <typename T>
T identity(T v) {
  return v;
}

}  // namespace detail

/**
 * WrapperView is a helper class for classes that wrap Flatbuffer objects in a
 * nicer interface, like Step and CompiledManifest. It is used in places where
 * they have fields that are arrays, where each item in the array should be
 * wrapped in something else, for example a flatbuffers::String * that should be
 * converted to a nt_string_view.
 *
 * This class allows exposing such lists of items in an std::vector-like
 * interface without actually iterating over all the items at construction time.
 * It is a view that wraps the items lazily.
 */
template <
    typename Iter,
    typename Wrapper,
    Wrapper (Wrap(decltype(*std::declval<Iter>()))) = detail::identity>
class WrapperView {
  using wrapped_type = Wrapper;
 public:
  using value_type =
      typename std::remove_const<
          typename std::remove_reference<Wrapper>::type>::type;
  using size_type = size_t;
  using diff_type = ptrdiff_t;
  using reference = value_type &;
  using const_reference = const value_type &;
  using pointer = value_type *;
  using const_pointer = const value_type *;

  class iterator : public std::iterator<
      std::random_access_iterator_tag,
      value_type,
      diff_type> {
   public:
    explicit iterator(Iter i) : _i(i) {}
    iterator(const iterator &) = default;
    iterator(iterator &&) = default;
    iterator &operator=(const iterator &) = default;
    iterator &operator=(iterator &&) = default;

    friend void swap(iterator &a, iterator &b) {
      std::swap(a._i, b._i);
    }

    wrapped_type operator*() const {
      return Wrap(*_i);
    }

    // Lacking operator-> because we can't return a pointer to Wrap(*_i)

    iterator &operator++() {
      ++_i;
      return *this;
    }

    iterator operator++(int) {
      auto copy = *this;
      ++_i;
      return copy;
    }

    iterator &operator--() {
      --_i;
      return *this;
    }

    iterator operator--(int) {
      auto copy = *this;
      --_i;
      return copy;
    }

    bool operator==(iterator other) const {
      return _i == other._i;
    }

    bool operator!=(iterator other) const {
      return _i != other._i;
    }

    iterator operator+(diff_type diff) const {
      return iterator(_i + diff);
    }

    iterator operator-(diff_type diff) const {
      return iterator(_i - diff);
    }

    iterator &operator+=(diff_type diff) {
      _i += diff;
      return *this;
    }

    iterator &operator-=(diff_type diff) {
      _i -= diff;
      return *this;
    }

    diff_type operator-(iterator other) const {
      return _i - other._i;
    }

   private:
    Iter _i;
  };

  using const_iterator = iterator;
  using inner_iterator = Iter;

  WrapperView() = default;
  WrapperView(Iter begin, Iter end)
      : _begin(begin), _end(end) {}
  WrapperView(const WrapperView &) = default;
  WrapperView(WrapperView &&) = default;
  WrapperView &operator=(const WrapperView &) = default;
  WrapperView &operator=(WrapperView &&) = default;

  wrapped_type at(size_type pos) const {
    if (!(pos < size())) {
      throw std::out_of_range("WrapperView");
    }
    return Wrap(*(_begin + pos));
  }

  wrapped_type operator[](size_type pos) const {
    return Wrap(*(_begin + pos));
  }

  wrapped_type front() const {
    return Wrap(*_begin);
  }

  wrapped_type back() const {
    return Wrap(*(_end - 1));
  }

  const_iterator begin() const {
    return iterator(_begin);
  }

  const_iterator cbegin() const {
    return iterator(_begin);
  }

  const_iterator end() const {
    return iterator(_end);
  }

  const_iterator cend() const {
    return iterator(_end);
  }

  bool empty() const {
    return _begin == _end;
  }

  size_type size() const {
    return _end - _begin;
  }

  friend void swap(WrapperView &a, WrapperView &b) {
    std::swap(a._begin, b._begin);
    std::swap(a._end, b._end);
  }

  bool operator==(WrapperView other) const {
    if (other._begin == _begin && other._end == _end) {
      return true;
    }

    if (other.size() != size()) {
      return false;
    }

    for (size_type i = 0; i < size(); i++) {
      if ((*this)[i] != other[i]) {
        return false;
      }
    }

    return true;
  }

  bool operator!=(WrapperView other) const {
    return !(*this == other);
  }

 private:
  Iter _begin{};
  Iter _end{};
};

}  // namespace shk
