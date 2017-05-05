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

#include "fs/file_system.h"

namespace shk {

/**
 * A FileId consists of an inode number and a device number. It uniquely
 * identifies a file on the file system. Please note that the device number
 * is not stable over time for all file systems (most notably network file
 * systems) so it should not be persisted between build invocations.
 */
struct FileId {
  FileId() = default;
  FileId(ino_t ino, dev_t dev)
      : ino(ino), dev(dev) {}
  FileId(const Stat &stat)
      : ino(stat.metadata.ino), dev(stat.metadata.dev) {}

  /**
   * Returns true if the FileId refers to a file that does not exist.
   */
  bool missing() const {
    return ino == 0 && dev == 0;
  }

  ino_t ino = 0;
  dev_t dev = 0;
};

inline bool operator==(const FileId &a, const FileId &b) {
  return
    a.ino == b.ino &&
    a.dev == b.dev;
}

inline bool operator!=(const FileId &a, const FileId &b) {
  return !(a == b);
}

}  // namespace shk

namespace std {

template<>
struct hash<shk::FileId> {
  using argument_type = shk::FileId;
  using result_type = std::size_t;

  result_type operator()(const argument_type &file_id) const {
    // The inode number is probably unique anyway
    return static_cast<uintptr_t>(file_id.ino);
  }
};

}  // namespace std
