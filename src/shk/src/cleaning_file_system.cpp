#include "cleaning_file_system.h"

#include <errno.h>

namespace shk {

CleaningFileSystem::CleaningFileSystem(FileSystem &inner_file_system)
    : _inner(inner_file_system) {}

int CleaningFileSystem::getRemovedCount() const {
  return _removed_count;
}

std::unique_ptr<FileSystem::Stream> CleaningFileSystem::open(
    const std::string &path, const char *mode) throw(IoError) {
  return _inner.open(path, mode);
}

std::unique_ptr<FileSystem::Mmap> CleaningFileSystem::mmap(
    const std::string &path) throw(IoError) {
  return _inner.mmap(path);
}

Stat CleaningFileSystem::stat(const std::string &path) {
  Stat stat;
  stat.result = ENOENT;
  return stat;
}

Stat CleaningFileSystem::lstat(const std::string &path) {
  Stat stat;
  stat.result = ENOENT;
  return stat;
}

void CleaningFileSystem::mkdir(const std::string &path)
    throw(IoError) {
  // Don't make directories; the build process creates directories
  // for things that are about to be built.
}

void CleaningFileSystem::rmdir(const std::string &path)
    throw(IoError) {
  _inner.rmdir(path);
  _removed_count++;
}

void CleaningFileSystem::unlink(const std::string &path)
    throw(IoError) {
  _inner.unlink(path);
  _removed_count++;
}

void CleaningFileSystem::rename(
    const std::string &old_path,
    const std::string &new_path) throw(IoError) {
  _inner.rename(old_path, new_path);
}

void CleaningFileSystem::truncate(
    const std::string &path, size_t size) throw(IoError) {
  _inner.truncate(path, size);
}

std::vector<DirEntry> CleaningFileSystem::readDir(
    const std::string &path) throw(IoError) {
  return _inner.readDir(path);
}

std::string CleaningFileSystem::readFile(const std::string &path)
    throw(IoError) {
  return _inner.readFile(path);
}

Hash CleaningFileSystem::hashFile(const std::string &path)
    throw(IoError) {
  return _inner.hashFile(path);
}

std::string CleaningFileSystem::mkstemp(
    std::string &&filename_template) throw(IoError) {
  return _inner.mkstemp(std::move(filename_template));
}

}  // namespace shk
