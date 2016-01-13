#include "in_memory_file_system.h"

namespace shk {
namespace detail {

std::pair<std::string, std::string> basenameSplit(const std::string &path) {
  const auto slash_pos = path.find_last_of('/');

  if (slash_pos == std::string::npos) {
    return std::make_pair("", path);
  } else {
    return std::make_pair(
        std::string(path.begin(), path.begin() + slash_pos),
        std::string(path.begin() + slash_pos + 1, path.end()));
  }
}

}  // namespace detail

InMemoryFileSystem::InMemoryFileSystem(Paths &paths)
    : _paths(&paths) {}

std::unique_ptr<FileSystem::Stream> InMemoryFileSystem::open(const Path &path, const char *mode) throw(IoError) {
  return nullptr;
  // TODO(peck): Implement me
}

Stat InMemoryFileSystem::stat(const Path &path) {
  // Symlinks are not supported so stat is the same as lstat
  return lstat(path);
}

Stat InMemoryFileSystem::lstat(const Path &path) {
  Stat stat;
  // TODO(peck): Implement me
  return stat;
}

void InMemoryFileSystem::mkdir(const Path &path) throw(IoError) {
  // TODO(peck): Implement me
}

void InMemoryFileSystem::rmdir(const Path &path) throw(IoError) {
  // TODO(peck): Implement me
}

void InMemoryFileSystem::unlink(const Path &path) throw(IoError) {
  // TODO(peck): Implement me
}

bool InMemoryFileSystem::operator==(const InMemoryFileSystem &other) const {
  return (
      _paths == other._paths &&
      _directories == other._directories);
}

bool InMemoryFileSystem::Directory::operator==(const Directory &other) const {
  return (
      files == other.files &&
      directories == other.directories);
}

InMemoryFileSystem::EntryType InMemoryFileSystem::entryType(const Path &path) const {
  std::string dirname;
  std::string basename;
  std::tie(dirname, basename) = detail::basenameSplit(path.canonicalized());
  const Path dir_path = _paths->get(dirname);

  const auto it = _directories.find(dir_path);
  if (it == _directories.end()) {
    return EntryType::DOES_NOT_EXIST;
  }

  const auto &directory = it->second;

  if (directory.files.count(basename)) {
    return EntryType::FILE;
  } else if (directory.directories.count(basename)) {
    return EntryType::DIRECTORY;
  } else {
    return EntryType::DOES_NOT_EXIST;
  }
}

}  // namespace shk
