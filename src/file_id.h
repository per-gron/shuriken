#pragma once

#include "file_system.h"

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
