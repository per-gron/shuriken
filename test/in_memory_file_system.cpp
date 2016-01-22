#include "in_memory_file_system.h"

#include <errno.h>
#include <sys/stat.h>

namespace shk {
namespace {

std::string dirname(const std::string &path) {
  return detail::basenameSplitPiece(path).first.asString();
}

}  // anonymous namespace

InMemoryFileSystem::InMemoryFileSystem(Paths &paths)
    : _paths(&paths) {
  _directories[paths.get(".")];
}

Paths &InMemoryFileSystem::paths() {
  return *_paths;
}

std::unique_ptr<FileSystem::Stream> InMemoryFileSystem::open(
    const Path &path, const char *mode) throw(IoError) {
  const auto mode_string = std::string(mode);
  bool read = false;
  bool write = false;
  bool truncate_or_create = false;
  if (mode_string == "r") {
    read = true;
    write = false;
  } else if (mode_string == "r+") {
    read = true;
    write = false;
  } else if (mode_string == "w") {
    read = false;
    write = true;
    truncate_or_create = true;
  } else if (mode_string == "w+") {
    read = true;
    write = true;
    truncate_or_create = true;
  }

  const auto l = lookup(path);
  switch (l.entry_type) {
  case EntryType::DIRECTORY_DOES_NOT_EXIST:
    throw IoError("A component of the path prefix is not a directory", ENOTDIR);
  case EntryType::DIRECTORY:
    throw IoError("The named file is a directory", EISDIR);
  case EntryType::FILE_DOES_NOT_EXIST:
    if (!truncate_or_create) {
      throw IoError("No such file or directory", ENOENT);
    }
    {
      const auto &file = std::make_shared<File>();
      l.directory->files[l.basename] = file;
      return std::unique_ptr<Stream>(
          new InMemoryFileStream(file, read, write));
    }
  case EntryType::FILE:
    {
      const auto &file = l.directory->files[l.basename];
      if (truncate_or_create) {
        file->contents.clear();
      }
      return std::unique_ptr<Stream>(
          new InMemoryFileStream(file, read, write));
    }
  }
}

Stat InMemoryFileSystem::stat(const Path &path) {
  // Symlinks are not supported so stat is the same as lstat
  return lstat(path);
}

Stat InMemoryFileSystem::lstat(const Path &path) {
  Stat stat;

  const auto l = lookup(path);
  switch (l.entry_type) {
  case EntryType::DIRECTORY_DOES_NOT_EXIST:
    stat.result = ENOTDIR;
    break;
  case EntryType::FILE_DOES_NOT_EXIST:
    stat.result = ENOENT;
    break;
  case EntryType::FILE:
  case EntryType::DIRECTORY:
    stat.metadata.mode = 0755;  // Pretend this is the umask
    if (l.entry_type == EntryType::FILE) {
      stat.metadata.size = l.directory->files[l.basename]->contents.size();
      stat.metadata.mode |= S_IFREG;
    } else {
      stat.metadata.mode |= S_IFDIR;
    }
    // TODO(peck): Set mtime and ctime
    break;
  }

  return stat;
}

void InMemoryFileSystem::mkdir(const Path &path) throw(IoError) {
  const auto l = lookup(path);
  switch (l.entry_type) {
  case EntryType::DIRECTORY_DOES_NOT_EXIST:
    throw IoError("A component of the path prefix is not a directory", ENOTDIR);
    break;
  case EntryType::FILE:
  case EntryType::DIRECTORY:
    throw IoError("The named file exists", EEXIST);
    break;
  case EntryType::FILE_DOES_NOT_EXIST:
    l.directory->directories.insert(l.basename);
    _directories[path];
    break;
  }
}

void InMemoryFileSystem::rmdir(const Path &path) throw(IoError) {
  const auto l = lookup(path);
  switch (l.entry_type) {
  case EntryType::DIRECTORY_DOES_NOT_EXIST:
    throw IoError("A component of the path prefix is not a directory", ENOTDIR);
    break;
  case EntryType::FILE_DOES_NOT_EXIST:
    throw IoError("The named directory does not exist", ENOENT);
    break;
  case EntryType::FILE:
    throw IoError("The named directory is a file", EPERM);
    break;
  case EntryType::DIRECTORY:
    const auto &dir = _directories[path];
    if (!dir.empty()) {
      throw IoError(
          "The named directory contains files other than `.' and `..' in it",
          ENOTEMPTY);
    } else {
      l.directory->directories.erase(l.basename);
      _directories.erase(path);
    }
    break;
  }
}

void InMemoryFileSystem::unlink(const std::string &path) throw(IoError) {
  const auto l = lookup(_paths->get(path));
  switch (l.entry_type) {
  case EntryType::DIRECTORY_DOES_NOT_EXIST:
    throw IoError("A component of the path prefix is not a directory", ENOTDIR);
    break;
  case EntryType::FILE_DOES_NOT_EXIST:
    throw IoError("The named file does not exist", ENOENT);
    break;
  case EntryType::DIRECTORY:
    throw IoError("The named file is a directory", EPERM);
    break;
  case EntryType::FILE:
    l.directory->files.erase(l.basename);
    break;
  }
}

std::string InMemoryFileSystem::readFile(const Path &path) throw(IoError) {
  std::string result;

  const auto stream = open(path, "r");
  uint8_t buf[1024];
  while (!stream->eof()) {
    size_t read_bytes = stream->read(buf, 1, sizeof(buf));
    result.append(reinterpret_cast<char *>(buf), read_bytes);
  }

  return result;
}

std::string InMemoryFileSystem::mkstemp(
    std::string &&filename_template) throw(IoError) {
  for (;;) {
    std::string filename = filename_template;
    if (mktemp(&filename[0]) == NULL) {
      throw IoError(
          std::string("Failed to create path for temporary file: ") +
          strerror(errno),
          errno);
    }
    // This is potentially an infinite loop… but since this is for testing I
    // don't care to do anything about that.
    if (stat(paths().get(filename)).result == ENOENT) {
      filename_template = std::move(filename);
      writeFile(*this, paths().get(filename_template), "");
      return filename_template;
    }
  }
}

bool InMemoryFileSystem::operator==(const InMemoryFileSystem &other) const {
  return (
      _paths == other._paths &&
      _directories == other._directories);
}

bool InMemoryFileSystem::Directory::empty() const {
  return files.empty() && directories.empty();
}

bool InMemoryFileSystem::Directory::operator==(const Directory &other) const {
  return (
      files == other.files &&
      directories == other.directories);
}

InMemoryFileSystem::InMemoryFileStream::InMemoryFileStream(
    const std::shared_ptr<File> &file,
    bool read,
    bool write)
    : _read(read),
      _write(write),
      _file(file) {}

size_t InMemoryFileSystem::InMemoryFileStream::read(
    uint8_t *ptr, size_t size, size_t nitems) throw(IoError) {
  if (!_read) {
    throw IoError("Attempted read from a write only stream", 0);
  }
  checkNotEof();

  const auto bytes = size * nitems;
  const auto bytes_remaining = _file->contents.size() - _position;
  if (bytes > bytes_remaining) {
    _eof = true;
  }

  const auto items_to_read = std::min(bytes_remaining, bytes) / size;
  const auto bytes_to_read = items_to_read * size;

  const auto it = _file->contents.begin() + _position;
  std::copy(
      it,
      it + bytes_to_read,
      reinterpret_cast<char *>(ptr));
  _position += bytes_to_read;

  return items_to_read;
}

void InMemoryFileSystem::InMemoryFileStream::write(
  const uint8_t *ptr, size_t size, size_t nitems) throw(IoError) {
  if (!_write) {
    throw IoError("Attempted write to a read only stream", 0);
  }
  checkNotEof();

  const auto bytes = size * nitems;
  const auto new_size = _position + bytes;
  if (_file->contents.size() < new_size) {
    _file->contents.resize(new_size);
  }
  std::copy(
      reinterpret_cast<const char *>(ptr),
      reinterpret_cast<const char *>(ptr + bytes),
      _file->contents.begin() + _position);
  _position += bytes;
}

long InMemoryFileSystem::InMemoryFileStream::tell() const throw(IoError) {
  return _position;
}

bool InMemoryFileSystem::InMemoryFileStream::eof() const {
  return _eof;
}

void InMemoryFileSystem::InMemoryFileStream::checkNotEof()
    const throw(IoError) {
  if (_eof) {
    throw IoError("Attempted to write to file that is past eof", 0);
  }
}

InMemoryFileSystem::LookupResult InMemoryFileSystem::lookup(const Path &path) {
  LookupResult result;

  StringPiece dirname_piece;
  StringPiece basename_piece;
  std::tie(dirname_piece, basename_piece) = detail::basenameSplitPiece(path.canonicalized());
  const auto dirname = dirname_piece.asString();
  const auto basename = basename_piece.asString();
  const Path dir_path = _paths->get(dirname);

  const auto it = _directories.find(dir_path);
  if (it == _directories.end()) {
    result.entry_type = EntryType::DIRECTORY_DOES_NOT_EXIST;
    return result;
  }

  auto &directory = it->second;

  if (directory.files.count(basename)) {
    result.entry_type = EntryType::FILE;
  } else if (directory.directories.count(basename)) {
    result.entry_type = EntryType::DIRECTORY;
  } else {
    result.entry_type = EntryType::FILE_DOES_NOT_EXIST;
  }

  result.directory = &directory;
  result.basename = basename;
  return result;
}

void writeFile(
    FileSystem &file_system,
    const Path &path,
    const std::string &contents) throw(IoError) {
  const auto stream = file_system.open(path, "w");
  const auto * const data = reinterpret_cast<const uint8_t *>(contents.data());
  stream->write(data, 1, contents.size());
}

void mkdirs(FileSystem &file_system, const Path &path) throw(IoError) {
  if (path.canonicalized() == ".") {
    // Nothing left to do
    return;
  }

  const auto stat = file_system.stat(path);
  if (stat.result == ENOENT || stat.result == ENOTDIR) {
    const auto dirname = shk::dirname(path.canonicalized());
    mkdirs(file_system, file_system.paths().get(dirname));
    file_system.mkdir(path);
  } else if (S_ISDIR(stat.metadata.mode)) {
    // No need to do anything
  } else {
    // It exists and is not a directory
    throw IoError("Not a directory: " + path.canonicalized(), ENOTDIR);
  }
}

void mkdirsFor(FileSystem &file_system, const Path &path) throw(IoError) {
  const auto dirname = shk::dirname(path.canonicalized());
  mkdirs(file_system, file_system.paths().get(dirname));
}

}  // namespace shk
