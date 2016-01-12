#include "in_memory_file_system.h"

namespace shk {

std::unique_ptr<FileSystem::Stream> InMemoryFileSystem::open(const Path &path, const char *mode) throw(IoError) {
  return nullptr;
}

Stat InMemoryFileSystem::stat(const Path &path) {
  // Symlinks are not supported so stat is the same as lstat
  return lstat(path);
}

Stat InMemoryFileSystem::lstat(const Path &path) {
  Stat stat;
  return stat;
}

void InMemoryFileSystem::mkdir(const Path &path) throw(IoError) {
}

void InMemoryFileSystem::rmdir(const Path &path) throw(IoError) {
}

void InMemoryFileSystem::unlink(const Path &path) throw(IoError) {
}

bool operator==(const InMemoryFileSystem &a, const InMemoryFileSystem &b) {
  return true;  // TODO(peck): Implement me
}

}  // namespace shk
