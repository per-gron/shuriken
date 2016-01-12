#pragma once

#include <array>

namespace shk {

/**
 * The reason Hash isn't a simple using type alias is to be able to define a
 * std::hash function for it.
 */
struct Hash {
  std::array<uint8_t, 20> data;
};

bool operator==(const Hash &a, const Hash &b) {
  return a.data == b.data;
}

}  // namespace shk

namespace std
{

template<>
struct hash<shk::Hash> {
  using argument_type = shk::Hash;
  using result_type = std::size_t;

  result_type operator()(const argument_type &h) const {
    return *reinterpret_cast<const result_type *>(h.data.data());
  }
};

}
