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

#include "fs/file_system.h"

#include <algorithm>

#include <errno.h>
#include <sys/stat.h>

#include <blake2.h>
#include <util/path_error.h>
#include <util/path_operations.h>

#include "path.h"

namespace shk {

USE_RESULT std::pair<Hash, IoError> FileSystem::hashDir(
    nt_string_view path, string_view extra_data) {
  Hash hash;

  blake2b_state state;
  blake2b_init(&state, hash.data.size());

  blake2b_update(
      &state,
      reinterpret_cast<const uint8_t *>(extra_data.data()),
      sizeof(extra_data.size()));

  std::vector<DirEntry> dir_entries;
  IoError error;
  std::tie(dir_entries, error) = readDir(path);
  if (error) {
    return std::make_pair(Hash(), error);
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
  return std::make_pair(hash, IoError::success());
}

USE_RESULT std::pair<Hash, IoError> FileSystem::hashSymlink(
    nt_string_view path, string_view extra_data) {
  Hash hash;

  blake2b_state state;
  blake2b_init(&state, hash.data.size());

  blake2b_update(
      &state,
      reinterpret_cast<const uint8_t *>(extra_data.data()),
      sizeof(extra_data.size()));

  std::string link_target;
  IoError error;
  std::tie(link_target, error) = readSymlink(path);
  if (error) {
    return std::make_pair(Hash(), error);
  }

  blake2b_update(
      &state,
      reinterpret_cast<const uint8_t *>(link_target.c_str()),
      link_target.size());

  blake2b_final(&state, hash.data.data(), hash.data.size());

  return std::make_pair(hash, IoError::success());
}

USE_RESULT IoError FileSystem::writeFile(
    nt_string_view path, string_view contents) {
  std::unique_ptr<FileSystem::Stream> stream;
  IoError error;
  std::tie(stream, error) = open(path, "wb");
  if (error) {
    return error;
  }
  const auto * const data = reinterpret_cast<const uint8_t *>(contents.data());
  if (auto error = stream->write(data, 1, contents.size())) {
    return error;
  }
  return IoError::success();
}

namespace {

USE_RESULT IoError mkdirs(
    FileSystem &file_system,
    std::string noncanonical_path,
    std::vector<std::string> &created_dirs) {
  auto path = noncanonical_path;
  try {
    canonicalizePath(&path);
  } catch (const PathError &path_error) {
    return IoError(path_error.what(), 0);
  }
  if (path == "." || path == "/") {
    // Nothing left to do
    return IoError::success();
  }

  const auto stat = file_system.stat(path);
  if (stat.result == ENOENT || stat.result == ENOTDIR) {
    if (auto error =
            mkdirs(file_system, std::string(dirname(path)), created_dirs)) {
      return error;
    }
    created_dirs.push_back(path);
    if (auto error = file_system.mkdir(std::move(path))) {
      return error;
    }
  } else if (S_ISDIR(stat.metadata.mode)) {
    // No need to do anything
  } else {
    // It exists and is not a directory
    return IoError("Not a directory: " + path, ENOTDIR);
  }

  return IoError::success();
}

}  // anonymous namespace

USE_RESULT std::pair<std::vector<std::string>, IoError> mkdirs(
    FileSystem &file_system,
    nt_string_view noncanonical_path) {
  std::vector<std::string> created_dirs;
  auto error = mkdirs(
      file_system, std::string(noncanonical_path), created_dirs);
  return { std::move(created_dirs), error };
}

}  // namespace shk
