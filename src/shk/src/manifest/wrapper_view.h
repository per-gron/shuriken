#pragma once

#include <utility>
#include <type_traits>

namespace shk {

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
    Wrapper (Wrap(decltype(*std::declval<Iter>())))>
class WrapperView {
 public:
  using value_type = Wrapper;
  using size_type = size_t;
  using diff_type = ptrdiff_t;
  using reference = value_type &;
  using const_reference = const value_type &;
  using pointer = value_type *;
  using const_pointer = const value_type *;

  class iterator {
   public:
    explicit iterator(Iter i) : _i(i) {}
    iterator(const iterator &) = default;
    iterator(iterator &&) = default;
    iterator &operator=(const iterator &) = default;
    iterator &operator=(iterator &&) = default;

    friend void swap(iterator &a, iterator &b) {
      std::swap(a._i, b._i);
    }

    value_type operator*() const {
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

    diff_type operator-(iterator other) const {
      return _i - other._i;
    }

   private:
    Iter _i;
  };

  using const_iterator = iterator;

  WrapperView() = default;
  WrapperView(Iter begin, Iter end)
      : _begin(begin), _end(end) {}
  WrapperView(const WrapperView &) = default;
  WrapperView(WrapperView &&) = default;
  WrapperView &operator=(const WrapperView &) = default;
  WrapperView &operator=(WrapperView &&) = default;

  value_type at(size_type pos) const {
    if (!(pos < size())) {
      throw std::out_of_range("WrapperView");
    }
    return Wrap(*(_begin + pos));
  }

  value_type operator[](size_type pos) const {
    return Wrap(*(_begin + pos));
  }

  value_type front() const {
    return Wrap(*_begin);
  }

  value_type back() const {
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
