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

#include <deque>
#include <unordered_map>
#include <unordered_set>

namespace shk {

/**
 * FileSystem that is backed only by memory. Used for testing. In addition to
 * the FileSystem functionality, it is also copiable and offers an equality
 * operator, which is useful to see if a sequence of operations produce
 * identical results.
 */
class InMemoryFileSystem : public FileSystem {
 public:
  InMemoryFileSystem(const std::function<time_t ()> &clock = []{ return 0; });

  /**
   * Causes the next mkstemp to return path. Can be useful in tests that need to
   * predict temporary paths.
   */
  void enqueueMkstempResult(std::string &&path);

  std::unique_ptr<Stream> open(
      nt_string_view path, const char *mode) throw(IoError) override;
  std::unique_ptr<Mmap> mmap(nt_string_view path) throw(IoError) override;
  Stat stat(nt_string_view path) override;
  Stat lstat(nt_string_view path) override;
  USE_RESULT IoError mkdir(nt_string_view path) override;
  USE_RESULT IoError rmdir(nt_string_view path) override;
  USE_RESULT IoError unlink(nt_string_view path) override;
  USE_RESULT IoError symlink(
      nt_string_view target,
      nt_string_view source) override;
  USE_RESULT IoError rename(
      nt_string_view old_path,
      nt_string_view new_path) override;
  USE_RESULT IoError truncate(nt_string_view path, size_t size) override;
  USE_RESULT std::pair<std::vector<DirEntry>, IoError> readDir(
      nt_string_view path) override;
  USE_RESULT std::pair<std::string, IoError> readSymlink(
      nt_string_view path) override;
  USE_RESULT std::pair<std::string, IoError> readFile(
      nt_string_view path) override;
  USE_RESULT std::pair<Hash, IoError> hashFile(
      nt_string_view path) override;
  USE_RESULT std::pair<std::string, IoError> mkstemp(
      std::string &&filename_template) override;

  bool operator==(const InMemoryFileSystem &other) const;

 private:
  std::unique_ptr<Stream> open(
      bool expect_symlink,
      nt_string_view path,
      const char *mode) throw(IoError);

  Stat stat(bool follow_symlink, nt_string_view path);

  enum class EntryType {
    FILE_DOES_NOT_EXIST,
    DIRECTORY_DOES_NOT_EXIST,
    DIRECTORY,
    FILE,
  };

  struct File {
    File(ino_t ino) : ino(ino) {}

    time_t mtime = 0;
    ino_t ino;
    std::string contents;
    bool symlink = false;
  };

  struct Directory {
    Directory() = default;
    Directory(time_t mtime, ino_t ino) :
        mtime(mtime), ino(ino) {}
    Directory(const Directory &other) = default;
    Directory(Directory &&other) = default;
    Directory &operator=(const Directory &other) = default;
    Directory &operator=(Directory &&other) = default;

    time_t mtime = 0;
    ino_t ino = 0;

    /**
     * Key is the basename of the file, value is the contents of the file. It's
     * a shared pointer to make it possible to keep a stream to it open even
     * after unlinking it.
     */
    std::unordered_map<std::string, std::shared_ptr<File>> files;

    std::unordered_set<std::string> directories;

    bool empty() const;
    bool operator==(const Directory &other) const;
  };

  struct LookupResult {
    EntryType entry_type = EntryType::FILE_DOES_NOT_EXIST;
    Directory *directory = nullptr;
    std::string basename;
    std::string canonicalized;
  };

  class InMemoryFileStream : public Stream {
   public:
    InMemoryFileStream(
        const std::function<time_t ()> &clock,
        const std::shared_ptr<File> &file,
        bool read,
        bool write,
        bool append);

    size_t read(
        uint8_t *ptr, size_t size, size_t nitems) throw(IoError) override;
    USE_RESULT IoError write(
        const uint8_t *ptr, size_t size, size_t nitems) override;
    long tell() const throw(IoError) override;
    bool eof() const override;

   private:
    USE_RESULT IoError checkNotEof() const;

    const std::function<time_t ()> _clock;
    const bool _read;
    const bool _write;
    bool _eof = false;
    size_t _position;
    const std::shared_ptr<File> _file;
  };

  class InMemoryMmap : public Mmap {
   public:
    InMemoryMmap(const std::shared_ptr<File> &file);

    string_view memory() override;

   private:
    const std::shared_ptr<File> _file;
  };

  LookupResult lookup(nt_string_view path);

  std::deque<std::string> _mkstemp_paths;
  const std::function<time_t ()> _clock;
  std::unordered_map<std::string, Directory> _directories;
  ino_t _ino = 0;
};

}  // namespace shk
