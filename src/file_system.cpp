#include "file_system.h"

#include <algorithm>

#include <blake2.h>

namespace shk {

Hash FileSystem::hashDir(const std::string &path) throw(IoError) {
  Hash hash;

  blake2b_state state;
  blake2b_init(&state, hash.data.size());

  auto dir_entries = readDir(path);
  std::sort(dir_entries.begin(), dir_entries.end());
  for (const auto &dir_entry : dir_entries) {
    blake2b_update(
        &state,
        reinterpret_cast<const uint8_t *>(&dir_entry.type),
        sizeof(dir_entry.type));
    blake2b_update(
        &state,
        reinterpret_cast<const uint8_t *>(dir_entry.name.c_str()),
        dir_entry.name.size() + 1);  // Include the trailing \0
  }

  blake2b_final(&state, hash.data.data(), hash.data.size());
  return hash;
}

void FileSystem::writeFile(
    const std::string &path,
    const std::string &contents) throw(IoError) {
  const auto stream = open(path, "w");
  const auto * const data = reinterpret_cast<const uint8_t *>(contents.data());
  stream->write(data, 1, contents.size());
}

}  // namespace shk
