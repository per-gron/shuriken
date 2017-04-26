#include "fs/file_system.h"

#include <algorithm>

#include <errno.h>
#include <sys/stat.h>

#include <blake2.h>

#include "path.h"

namespace shk {

std::pair<Hash, bool> FileSystem::hashDir(
    nt_string_view path, std::string *err) {
  Hash hash;

  blake2b_state state;
  blake2b_init(&state, hash.data.size());

  std::vector<DirEntry> dir_entries;
  bool success;
  std::tie(dir_entries, success) = readDir(path, err);
  if (!success) {
    return std::make_pair(Hash(), false);
  }
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
  return std::make_pair(hash, true);
}

std::pair<Hash, bool> FileSystem::hashSymlink(
    nt_string_view path, std::string *err) {
  Hash hash;

  blake2b_state state;
  blake2b_init(&state, hash.data.size());

  std::string link_target;
  bool success;
  std::tie(link_target, success) = readSymlink(path, err);
  if (!success) {
    return std::make_pair(Hash(), false);
  }

  blake2b_update(
      &state,
      reinterpret_cast<const uint8_t *>(link_target.c_str()),
      link_target.size());

  blake2b_final(&state, hash.data.data(), hash.data.size());

  return std::make_pair(hash, true);
}

bool FileSystem::writeFile(
    nt_string_view path, string_view contents, std::string *err) {
  try {
    const auto stream = open(path, "wb");
    const auto * const data = reinterpret_cast<const uint8_t *>(contents.data());
    stream->write(data, 1, contents.size());
  } catch (const IoError &io_error) {
    *err = io_error.what();
    return false;
  }
  return true;
}

namespace {

void mkdirs(
    FileSystem &file_system,
    std::string noncanonical_path,
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
    mkdirs(file_system, std::string(dirname(path)), created_dirs);
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
    nt_string_view noncanonical_path) throw(IoError) {
  std::vector<std::string> created_dirs;
  mkdirs(file_system, std::string(noncanonical_path), created_dirs);
  return created_dirs;
}

}  // namespace shk
