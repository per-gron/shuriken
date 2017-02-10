#include "fs/file_system.h"

#include <algorithm>

#include <errno.h>
#include <sys/stat.h>

#include <blake2.h>

#include "path.h"

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
  const auto stream = open(path, "wb");
  const auto * const data = reinterpret_cast<const uint8_t *>(contents.data());
  stream->write(data, 1, contents.size());
}

namespace {

void mkdirs(
    FileSystem &file_system,
    const std::string &noncanonical_path,
    std::vector<std::string> &created_dirs) throw(IoError) {
  auto path = noncanonical_path;
  try {
    canonicalizePath(&path);
  } catch (const PathError &path_error) {
    throw IoError(path_error.what(), 0);
  }
  if (path == "." || path == "/") {
    // Nothing left to do
    return;
  }

  const auto stat = file_system.stat(path);
  if (stat.result == ENOENT || stat.result == ENOTDIR) {
    const auto dirname = shk::dirname(path);
    mkdirs(file_system, dirname, created_dirs);
    created_dirs.push_back(path);
    file_system.mkdir(std::move(path));
  } else if (S_ISDIR(stat.metadata.mode)) {
    // No need to do anything
  } else {
    // It exists and is not a directory
    throw IoError("Not a directory: " + path, ENOTDIR);
  }
}

}  // anonymous namespace

std::vector<std::string> mkdirs(
    FileSystem &file_system,
    const std::string &noncanonical_path) throw(IoError) {
  std::vector<std::string> created_dirs;
  mkdirs(file_system, noncanonical_path, created_dirs);
  return created_dirs;
}

std::vector<std::string> mkdirsFor(
    FileSystem &file_system, const std::string &path) throw(IoError) {
  const auto dirname = shk::dirname(path);
  return mkdirs(file_system, dirname);
}

}  // namespace shk
