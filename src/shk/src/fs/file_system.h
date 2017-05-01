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

#include <vector>

#include <sys/types.h>

#include "hash.h"
#include "io_error.h"
#include "string_view.h"
#include "util.h"

namespace shk {

struct FileMetadata {
  int mode = 0;
  size_t size = 0;
  ino_t ino = 0;
  dev_t dev = 0;
};

struct Timestamps {
  time_t mtime;
  time_t ctime;
};

struct Stat {
  /**
   * Return value of stat.
   */
  int result = 0;
  FileMetadata metadata;
  Timestamps timestamps;
};

struct DirEntry {
  /**
   * Type of the DirEntry. These map to the type when using the readdir
   * function. The value of these is significant and should stay stable over
   * Shuriken versions, or strange things can happen when a directory is hashed.
   */
  enum class Type : uint8_t {
    UNKNOWN = 0,
    FIFO = 1,
    CHR = 2,
    DIR = 3,
    BLK = 4,
    REG = 5,
    LNK = 6,
    SOCK = 7,
    WHT = 8,
  };

  DirEntry() = default;
  DirEntry(Type type, std::string name)
      : type(type),
        name(std::move(name)) {}

  Type type = Type::UNKNOWN;
  std::string name;

  inline bool operator<(const DirEntry &other) const {
    return std::tie(name, type) < std::tie(other.name, other.type);
  }

  inline bool operator==(const DirEntry &other) const {
    return std::tie(name, type) == std::tie(other.name, other.type);
  }

  inline bool operator!=(const DirEntry &other) const {
    return !(*this == other);
  }
};

class FileSystem {
 public:
  virtual ~FileSystem() = default;

  class Stream {
   public:
    virtual ~Stream() = default;

    /**
     * Read ntitems objects, each size bytes long, storing them at the location
     * pointed to by ptr.
     * 
     * Returns the number of objects that were read. May be less if the end of
     * file was reached.
     */
    virtual size_t read(
        uint8_t *ptr, size_t size, size_t nitems) throw(IoError) = 0;
    /**
     * Write ntitems objects, each size bytes long, obtaining them from the
     * location given to by ptr.
     */
    virtual USE_RESULT IoError write(
        const uint8_t *ptr, size_t size, size_t nitems) = 0;
    virtual long tell() const throw(IoError) = 0;
    virtual bool eof() const = 0;
  };

  class Mmap {
   public:
    virtual ~Mmap() = default;

    virtual string_view memory() = 0;
  };

  virtual std::unique_ptr<Stream> open(
      nt_string_view path, const char *mode) throw(IoError) = 0;
  /**
   * Memory map a file in read only mode.
   */
  virtual std::unique_ptr<Mmap> mmap(
      nt_string_view path) throw(IoError) = 0;
  virtual Stat stat(nt_string_view path) = 0;
  virtual Stat lstat(nt_string_view path) = 0;
  virtual USE_RESULT IoError mkdir(nt_string_view path) = 0;
  virtual USE_RESULT IoError rmdir(nt_string_view path) = 0;
  virtual USE_RESULT IoError unlink(nt_string_view path) = 0;
  virtual USE_RESULT IoError symlink(
      nt_string_view target, nt_string_view source) = 0;
  virtual USE_RESULT IoError rename(
      nt_string_view old_path, nt_string_view new_path) = 0;
  virtual USE_RESULT IoError truncate(nt_string_view path, size_t size) = 0;
  /**
   * Return the files, directories and other entries in a given directory. Fails
   * if the path does not point to a directory. The returned entries are not
   * necessarily sorted in any particular order.
   */
  virtual USE_RESULT std::pair<std::vector<DirEntry>, IoError> readDir(
      nt_string_view path) = 0;

  /**
   * Utility function for hashing a directory. It is rather important that this
   * hash function works the same for all FileSystem implementations, so it is
   * defined directly here. It is implemented in terms of readDir.
   *
   * Please note that this only hashes the directory itself, with the list of
   * files that it contains. It does not hash the contents of those files or go
   * through subdirectories recursively.
   *
   * Returns the hash and a bool indicating if the operation was successful or
   * not.
   */
  std::pair<Hash, bool> hashDir(nt_string_view path, std::string *err);

  /**
   * Read the contents of a symlink.
   *
   * Returns the target of the symlink and a bool indicating if the operation
   * was successful or not.
   */
  virtual std::pair<std::string, bool> readSymlink(
      nt_string_view path, std::string *err) = 0;

  /**
   * Utility function for hashing a symlink. It is rather important that this
   * hash function works the same for all FileSystem implementations, so it is
   * defined directly here. It is implemented in terms of readlink.
   *
   * Returns the hash of the symlink and a bool indicating if the operation was
   * successful or not.
   */
  std::pair<Hash, bool> hashSymlink(nt_string_view path, std::string *err);

  /**
   * Utility function for reading files. It is on this interface because on
   * Windows reading the file as a whole is faster than reading it using Stream.
   */
  virtual std::pair<std::string, bool> readFile(
      nt_string_view path, std::string *err) = 0;

  /**
   * Utility function for hashing the contents of a file. This method uses
   * the blake2b hash function. Like readFile, it is directly on the FileSystem
   * interface because this is a highly performance sensitive operation.
   *
   * Returns the hash of the file and a bool indicating if the operation was
   * successful or not.
   */
  virtual std::pair<Hash, bool> hashFile(
      nt_string_view path, std::string *err) = 0;

  /**
   * Helper function for writing a string to a file.
   *
   * Returns true if the operation was successful
   */
  USE_RESULT IoError writeFile(nt_string_view path, string_view contents);

  /**
   * Create a temporary file that follows a template. See the man page for
   * mkstemp. This is necessary to have on the FileSystem interface for the
   * same reason mkstemp exists: mktemp that creates a temporary file path
   * often creates races when used, because it is possible that others will
   * create a file at that path between mktemp returns and when the file is
   * first created. mkstemp chooses a path and creates a file atomically,
   * avoiding this problem.
   *
   * Returns the path to the temporary file and a bool indicating whether the
   * operation was successful or not.
   */
  virtual std::pair<std::string, bool> mkstemp(
      std::string &&filename_template, std::string *err) = 0;
};

/**
 * Create directory and parent directories. Like mkdir -p
 *
 * Returns vector of paths to directories that were created.
 */
std::vector<std::string> mkdirs(
    FileSystem &file_system, nt_string_view path) throw(IoError);

}  // namespace shk
