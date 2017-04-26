#include "fs/cleaning_file_system.h"

#include <errno.h>

namespace shk {

CleaningFileSystem::CleaningFileSystem(FileSystem &inner_file_system)
    : _inner(inner_file_system) {}

int CleaningFileSystem::getRemovedCount() const {
  return _removed_count;
}

std::unique_ptr<FileSystem::Stream> CleaningFileSystem::open(
    nt_string_view path, const char *mode) throw(IoError) {
  return _inner.open(path, mode);
}

std::unique_ptr<FileSystem::Mmap> CleaningFileSystem::mmap(
    nt_string_view path) throw(IoError) {
  return _inner.mmap(path);
}

Stat CleaningFileSystem::stat(nt_string_view path) {
  Stat stat;
  stat.result = ENOENT;
  return stat;
}

Stat CleaningFileSystem::lstat(nt_string_view path) {
  Stat stat;
  stat.result = ENOENT;
  return stat;
}

void CleaningFileSystem::mkdir(nt_string_view path)
    throw(IoError) {
  // Don't make directories; the build process creates directories
  // for things that are about to be built.
}

void CleaningFileSystem::rmdir(nt_string_view path)
    throw(IoError) {
  _inner.rmdir(path);
  _removed_count++;
}

void CleaningFileSystem::unlink(nt_string_view path)
    throw(IoError) {
  _inner.unlink(path);
  _removed_count++;
}

void CleaningFileSystem::symlink(
    nt_string_view target, nt_string_view source) throw(IoError) {
  _inner.symlink(target, source);
}

void CleaningFileSystem::rename(
    nt_string_view old_path,
    nt_string_view new_path) throw(IoError) {
  _inner.rename(old_path, new_path);
}

void CleaningFileSystem::truncate(
    nt_string_view path, size_t size) throw(IoError) {
  _inner.truncate(path, size);
}

std::vector<DirEntry> CleaningFileSystem::readDir(
    nt_string_view path) throw(IoError) {
  return _inner.readDir(path);
}

std::pair<std::string, bool> CleaningFileSystem::readSymlink(
      nt_string_view path, std::string *err) {
  return _inner.readSymlink(path, err);
}

std::pair<std::string, bool> CleaningFileSystem::readFile(
      nt_string_view path, std::string *err) {
  return _inner.readFile(path, err);
}

std::pair<Hash, bool> CleaningFileSystem::hashFile(
      nt_string_view path, std::string *err) {
  return _inner.hashFile(path, err);
}

std::pair<std::string, bool> CleaningFileSystem::mkstemp(
      std::string &&filename_template, std::string *err) {
  return _inner.mkstemp(std::move(filename_template), err);
}

}  // namespace shk
