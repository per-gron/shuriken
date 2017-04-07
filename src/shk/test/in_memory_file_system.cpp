#include "in_memory_file_system.h"

#include <assert.h>
#include <errno.h>
#include <sys/stat.h>

#include <blake2.h>

#include "fs/path.h"

namespace shk {

InMemoryFileSystem::InMemoryFileSystem(const std::function<time_t ()> &clock)
    : _clock(clock) {
  _directories.emplace("/", Directory(clock(), _ino++));
}

void InMemoryFileSystem::enqueueMkstempResult(std::string &&path) {
  _mkstemp_paths.push_back(std::move(path));
}

std::unique_ptr<FileSystem::Stream> InMemoryFileSystem::open(
    const std::string &path, const char *mode) throw(IoError) {
  const auto mode_string = std::string(mode);
  bool read = false;
  bool write = false;
  bool truncate = false;
  bool create = false;
  bool append = false;
  if (mode_string == "r") {
    read = true;
    write = false;
  } else if (mode_string == "r+") {
    read = true;
    write = false;
  } else if (mode_string == "w" || mode_string == "wb") {
    read = false;
    write = true;
    truncate = true;
    create = true;
  } else if (mode_string == "w+") {
    read = true;
    write = true;
    truncate = true;
    create = true;
  } else if (mode_string == "a" || mode_string == "ab") {
    read = false;
    write = true;
    append = true;
    create = true;
  } else {
    throw IoError("Unsupported mode " + mode_string, 0);
  }

  const auto l = lookup(path);
  switch (l.entry_type) {
  case EntryType::DIRECTORY_DOES_NOT_EXIST:
    throw IoError("A component of the path prefix is not a directory", ENOTDIR);
  case EntryType::DIRECTORY:
    throw IoError("The named file is a directory", EISDIR);
  case EntryType::FILE_DOES_NOT_EXIST:
    if (!create) {
      throw IoError("No such file or directory", ENOENT);
    }
    {
      const auto &file = std::make_shared<File>(_ino++);
      file->mtime = _clock();
      l.directory->files[l.basename] = file;
      l.directory->mtime = _clock();
      return std::unique_ptr<Stream>(
          new InMemoryFileStream(_clock, file, read, write, append));
    }
  case EntryType::FILE:
    {
      const auto &file = l.directory->files[l.basename];
      if (truncate) {
        file->contents.clear();
      }
      return std::unique_ptr<Stream>(
          new InMemoryFileStream(_clock, file, read, write, append));
    }
  }
}

std::unique_ptr<FileSystem::Mmap> InMemoryFileSystem::mmap(
    const std::string &path) throw(IoError) {
  const auto l = lookup(path);
  switch (l.entry_type) {
  case EntryType::DIRECTORY_DOES_NOT_EXIST:
    throw IoError("A component of the path prefix is not a directory", ENOTDIR);
  case EntryType::DIRECTORY:
    throw IoError("The named file is a directory", EISDIR);
  case EntryType::FILE_DOES_NOT_EXIST:
    throw IoError("No such file or directory", ENOENT);
  case EntryType::FILE: {
    const auto &file = l.directory->files[l.basename];
    return std::unique_ptr<Mmap>(
        new InMemoryMmap(file));
  }
  }
}

Stat InMemoryFileSystem::stat(const std::string &path) {
  // Symlinks are not supported so stat is the same as lstat
  return lstat(path);
}

Stat InMemoryFileSystem::lstat(const std::string &path) {
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
      const auto &file = l.directory->files[l.basename];
      stat.metadata.size = file->contents.size();
      stat.metadata.ino = file->ino;
      stat.metadata.mode |= S_IFREG;
      stat.timestamps.mtime = file->mtime;
      stat.timestamps.ctime = file->mtime;
    } else {
      const auto &dir = _directories.find(l.canonicalized)->second;
      stat.metadata.ino = dir.ino;
      stat.metadata.mode |= S_IFDIR;
      stat.timestamps.mtime = dir.mtime;
      stat.timestamps.ctime = dir.mtime;
    }
    // TODO(peck): Set mtime and ctime
    break;
  }

  return stat;
}

void InMemoryFileSystem::mkdir(const std::string &path) throw(IoError) {
  const auto l = lookup(path);
  switch (l.entry_type) {
  case EntryType::DIRECTORY_DOES_NOT_EXIST:
    throw IoError("A component of the path prefix is not a directory", ENOTDIR);
  case EntryType::FILE:
  case EntryType::DIRECTORY:
    throw IoError("The named file exists", EEXIST);
  case EntryType::FILE_DOES_NOT_EXIST:
    l.directory->directories.emplace(l.basename);
    _directories.emplace(l.canonicalized, Directory(_clock(), _ino++));
    break;
  }
}

void InMemoryFileSystem::rmdir(const std::string &path) throw(IoError) {
  const auto l = lookup(path);
  switch (l.entry_type) {
  case EntryType::DIRECTORY_DOES_NOT_EXIST:
    throw IoError("A component of the path prefix is not a directory", ENOTDIR);
  case EntryType::FILE_DOES_NOT_EXIST:
    throw IoError("The named directory does not exist", ENOENT);
  case EntryType::FILE:
    throw IoError("The named directory is a file", EPERM);
  case EntryType::DIRECTORY:
    const auto &dir = _directories.find(l.canonicalized)->second;
    if (!dir.empty()) {
      throw IoError(
          "The named directory contains files other than `.' and `..' in it",
          ENOTEMPTY);
    } else {
      l.directory->directories.erase(l.basename);
      l.directory->mtime = _clock();
      _directories.erase(l.canonicalized);
    }
    break;
  }
}

void InMemoryFileSystem::unlink(const std::string &path) throw(IoError) {
  const auto l = lookup(path);
  switch (l.entry_type) {
  case EntryType::DIRECTORY_DOES_NOT_EXIST:
    throw IoError("A component of the path prefix is not a directory", ENOTDIR);
  case EntryType::FILE_DOES_NOT_EXIST:
    throw IoError("The named file does not exist", ENOENT);
  case EntryType::DIRECTORY:
    throw IoError("The named file is a directory", EPERM);
  case EntryType::FILE:
    l.directory->files.erase(l.basename);
    l.directory->mtime = _clock();
    break;
  }
}

void InMemoryFileSystem::rename(
    const std::string &old_path,
    const std::string &new_path) throw(IoError) {
  const auto old_l = lookup(old_path);
  const auto new_l = lookup(new_path);

  switch (old_l.entry_type) {
  case EntryType::DIRECTORY_DOES_NOT_EXIST:
    throw IoError("A component of the path prefix is not a directory", ENOTDIR);

  case EntryType::FILE_DOES_NOT_EXIST:
    throw IoError("The named file does not exist", ENOENT);

  case EntryType::DIRECTORY:
    switch (new_l.entry_type) {
    case EntryType::DIRECTORY_DOES_NOT_EXIST:
      throw IoError("A component of the path prefix is not a directory", ENOTDIR);
      break;
    case EntryType::FILE:
      throw IoError("The new file exists but is not a directory", ENOTDIR);
    case EntryType::DIRECTORY:
      if (new_path != old_path) {
        rmdir(new_path);
      }
      [[clang::fallthrough]];
    case EntryType::FILE_DOES_NOT_EXIST:
      old_l.directory->directories.erase(old_l.basename);
      new_l.directory->directories.insert(new_l.basename);
      old_l.directory->mtime = _clock();
      new_l.directory->mtime = _clock();

      std::unordered_map<std::string, std::string> dirs_to_rename;
      for (const auto &dir : _directories) {
        const auto &dir_name = dir.first;
        if (dir_name.size() < old_l.canonicalized.size()) {
          continue;
        }
        if (std::equal(
                dir_name.begin(),
                dir_name.end(),
                old_l.canonicalized.begin())) {
          // Need to move entries in _directories around, but that cannot be
          // done while iterating over it.
          dirs_to_rename[dir_name] =
              new_l.canonicalized +
              dir_name.substr(old_l.canonicalized.size());
        }
      }

      for (const auto &dir_to_rename : dirs_to_rename) {
        if (dir_to_rename.first != dir_to_rename.second) {
          auto old_dir = std::move(_directories.find(dir_to_rename.first)->second);
          _directories.erase(dir_to_rename.first);
          _directories[dir_to_rename.second] = std::move(old_dir);
        }
      }
      break;
    }
    break;

  case EntryType::FILE:
    switch (new_l.entry_type) {
    case EntryType::DIRECTORY_DOES_NOT_EXIST:
      throw IoError("A component of the path prefix is not a directory", ENOTDIR);
    case EntryType::DIRECTORY:
      throw IoError("The new file is a directory", EISDIR);
      break;
    case EntryType::FILE:
      if (new_path != old_path) {
        unlink(new_path);
      }
      [[clang::fallthrough]];
    case EntryType::FILE_DOES_NOT_EXIST:
      const auto contents = readFile(old_path);
      unlink(old_path);
      writeFile(new_path, contents);
      break;
    }
    break;
  }
}

void InMemoryFileSystem::truncate(
    const std::string &path, size_t size) throw(IoError) {
  const auto l = lookup(path);
  switch (l.entry_type) {
  case EntryType::DIRECTORY_DOES_NOT_EXIST:
    throw IoError("A component of the path prefix is not a directory", ENOTDIR);
  case EntryType::FILE_DOES_NOT_EXIST:
    throw IoError("The named file does not exist", ENOENT);
  case EntryType::DIRECTORY:
    throw IoError("The named file is a directory", EPERM);
  case EntryType::FILE:
    const auto file = l.directory->files.find(l.basename)->second;
    file->contents.resize(size);
    file->mtime = _clock();
    break;
  }
}

std::vector<DirEntry> InMemoryFileSystem::readDir(
    const std::string &path) throw(IoError) {
  std::vector<DirEntry> result;
  const auto l = lookup(path);
  switch (l.entry_type) {
  case EntryType::DIRECTORY_DOES_NOT_EXIST:
    throw IoError("A component of the path prefix is not a directory", ENOTDIR);
  case EntryType::FILE_DOES_NOT_EXIST:
    throw IoError("The named directory does not exist", ENOENT);
  case EntryType::FILE:
    throw IoError("The named directory is a file", EPERM);
  case EntryType::DIRECTORY:
    const auto &dir = _directories.find(l.canonicalized)->second;
    for (const auto &dir_path : dir.directories) {
      result.emplace_back(DirEntry::Type::DIR, dir_path);
    }
    for (const auto &file : dir.files) {
      result.emplace_back(DirEntry::Type::REG, file.first);
    }
    break;
  }

  return result;
}

std::string InMemoryFileSystem::readFile(const std::string &path) throw(IoError) {
  std::string result;

  const auto stream = open(path, "r");
  uint8_t buf[1024];
  while (!stream->eof()) {
    size_t read_bytes = stream->read(buf, 1, sizeof(buf));
    result.append(reinterpret_cast<char *>(buf), read_bytes);
  }

  return result;
}

Hash InMemoryFileSystem::hashFile(const std::string &path) throw(IoError) {
  // This is optimized for readability rather than speed
  Hash hash;
  blake2b_state state;
  blake2b_init(&state, hash.data.size());
  const auto file_contents = readFile(path);
  blake2b_update(
      &state,
      reinterpret_cast<const uint8_t *>(file_contents.data()),
      file_contents.size());
  blake2b_final(&state, hash.data.data(), hash.data.size());
  return hash;
}

std::string InMemoryFileSystem::mkstemp(
    std::string &&filename_template) throw(IoError) {
  if (!_mkstemp_paths.empty()) {
    auto result = std::move(_mkstemp_paths.front());
    _mkstemp_paths.pop_front();
    return result;
  }

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
    if (stat(filename).result == ENOENT) {
      filename_template = std::move(filename);
      writeFile(filename_template, "");
      return filename_template;
    }
  }
}

bool InMemoryFileSystem::operator==(const InMemoryFileSystem &other) const {
  return _directories == other._directories;
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
    const std::function<time_t ()> &clock,
    const std::shared_ptr<File> &file,
    bool read,
    bool write,
    bool append)
    : _clock(clock),
      _read(read),
      _write(write),
      _position(append ? file->contents.size() : 0),
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

  _file->mtime = _clock();
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

InMemoryFileSystem::InMemoryMmap::InMemoryMmap(const std::shared_ptr<File> &file)
    : _file(file) {}

StringPiece InMemoryFileSystem::InMemoryMmap::memory() {
  return StringPiece(_file->contents);
}

InMemoryFileSystem::LookupResult InMemoryFileSystem::lookup(
    const std::string &path) {
  LookupResult result;
  result.canonicalized = "/" + path;
  try {
    canonicalizePath(&result.canonicalized);
  } catch (const PathError &path_error) {
    result.entry_type = EntryType::DIRECTORY_DOES_NOT_EXIST;
    return result;
  }

  StringPiece dirname_piece;
  StringPiece basename_piece;
  std::tie(dirname_piece, basename_piece) = basenameSplitPiece(
      result.canonicalized);
  const auto dirname = dirname_piece.asString();
  const auto basename = basename_piece.asString();

  const auto it = _directories.find(dirname);
  if (it == _directories.end()) {
    result.entry_type = EntryType::DIRECTORY_DOES_NOT_EXIST;
    return result;
  }

  auto &directory = it->second;

  if (basename == "/") {
    result.entry_type = EntryType::DIRECTORY;
  } else if (directory.files.count(basename)) {
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

}  // namespace shk
