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

  Stat stat(nt_string_view path) override;

  Stat lstat(nt_string_view path) override;

  void mkdir(nt_string_view path) throw(IoError) override;

  void rmdir(nt_string_view path) throw(IoError) override;

  void unlink(nt_string_view path) throw(IoError) override;

  void symlink(
      nt_string_view target,
      nt_string_view source) throw(IoError) override;

  void rename(
      nt_string_view old_path,
      nt_string_view new_path) throw(IoError) override;

  void truncate(
      nt_string_view path, size_t size) throw(IoError) override;

  std::pair<std::vector<DirEntry>, bool> readDir(
      nt_string_view path, std::string *err) override;

  std::pair<std::string, bool> readSymlink(
      nt_string_view path, std::string *err) override;

  std::pair<std::string, bool> readFile(
      nt_string_view path, std::string *err) override;

  std::pair<Hash, bool> hashFile(
      nt_string_view path, std::string *err) override;

  std::pair<std::string, bool> mkstemp(
      std::string &&filename_template, std::string *err) override;

 private:
  FileSystem &_inner;
  int _removed_count = 0;
};

}  // namespace shk
