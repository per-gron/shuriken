#pragma once

#include "fs/file_system.h"

namespace shk {

/**
 * Used when cleaning.
 *
 * File system that acts like a normal file system, with some differences:
 *
 * 1. It counts the number of removed files, for reporting purposes.
 * 2. It lies about file stats, to ensure that everything is treated as dirty.
 * 3. It doesn't create directories.
 */
class CleaningFileSystem : public FileSystem {
 public:
  CleaningFileSystem(FileSystem &inner_file_system);

  int getRemovedCount() const;

  std::unique_ptr<Stream> open(
      nt_string_view path, const char *mode) throw(IoError) override;

  std::unique_ptr<Mmap> mmap(
      nt_string_view path) throw(IoError) override;

  Stat stat(const std::string &path) override;

  Stat lstat(const std::string &path) override;

  void mkdir(nt_string_view path) throw(IoError) override;

  void rmdir(const std::string &path) throw(IoError) override;

  void unlink(const std::string &path) throw(IoError) override;

  void symlink(
      const std::string &target,
      const std::string &source) throw(IoError) override;

  void rename(
      const std::string &old_path,
      const std::string &new_path) throw(IoError) override;

  void truncate(
      const std::string &path, size_t size) throw(IoError) override;

  std::vector<DirEntry> readDir(
      const std::string &path) throw(IoError) override;

  std::string readSymlink(const std::string &path) throw(IoError) override;

  std::string readFile(const std::string &path) throw(IoError) override;

  Hash hashFile(const std::string &path) throw(IoError) override;

  std::string mkstemp(
      std::string &&filename_template) throw(IoError) override;

 private:
  FileSystem &_inner;
  int _removed_count = 0;
};

}  // namespace shk
