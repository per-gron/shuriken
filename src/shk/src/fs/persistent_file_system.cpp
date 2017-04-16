// Copyright 2011 Google Inc. All Rights Reserved.
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


#include "fs/persistent_file_system.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/dir.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <blake2.h>

#include <util/raii_helper.h>

#include "nullterminated_string.h"

namespace shk {
namespace {

using FileHandle = RAIIHelper<FILE *, int, fclose>;

class PersistentFileSystem : public FileSystem {
  template<typename T>
  static T checkForMinusOne(T result) {
    if (result == -1) {
      throw IoError(strerror(errno), errno);
    } else {
      return result;
    }
  }

  class FileStream : public FileSystem::Stream {
   public:
    FileStream(nt_string_view path, const char *mode) throw(IoError)
        : _f(fopen(NullterminatedString(path).c_str(), mode)) {
      if (!_f.get()) {
        throw IoError(strerror(errno), errno);
      }
      fcntl(fileno(_f.get()), F_SETFD, FD_CLOEXEC);
    }

    size_t read(uint8_t *ptr, size_t size, size_t nitems) throw(IoError) override {
      auto result = fread(ptr, size, nitems, _f.get());
      if (eof()) {
        return result;
      } else if (ferror(_f.get()) != 0) {
        throw IoError("Failed to read from stream", 0);
      } else {
        assert(result == nitems);
        return result;
      }
    }

    void write(const uint8_t *ptr, size_t size, size_t nitems) throw(IoError) override {
      fwrite(ptr, size, nitems, _f.get());
      if (ferror(_f.get()) != 0) {
        throw IoError("Failed to write to stream", 0);
      }
    }

    long tell() const throw(IoError) override {
      return checkForMinusOne(ftell(_f.get()));
    }

    bool eof() const override {
      return feof(_f.get()) != 0;
    }

   private:
    FileHandle _f;
  };

  class FileMmap : public Mmap {
   public:
    FileMmap(nt_string_view path) {
      struct stat input;
      NullterminatedString nt_path(path);
      auto ret = ::stat(nt_path.c_str(), &input);
      if (ret == -1) {
        throw IoError(strerror(errno), errno);
      }
      _size = input.st_size;

      if (_size) {
        _f = ::open(nt_path.c_str(), O_RDONLY);
        if (_f == -1) {
          throw IoError(strerror(errno), errno);
        }

        _memory = ::mmap(nullptr, _size, PROT_READ, MAP_PRIVATE, _f, 0);
        if (_memory == MAP_FAILED) {
          throw IoError(strerror(errno), errno);
        }
      }
    }

    virtual ~FileMmap() {
      if (_memory != MAP_FAILED) {
        munmap(_memory, _size);
      }

      if (_f != -1) {
        close(_f);
      }
    }

    string_view memory() override {
      return string_view(static_cast<const char *>(_memory), _size);
    }

   private:
    size_t _size = 0;
    void *_memory = MAP_FAILED;
    int _f = -1;
  };

 public:
  std::unique_ptr<Stream> open(
      nt_string_view path, const char *mode) throw(IoError) override {
    return std::unique_ptr<Stream>(new FileStream(path, mode));
  }

  std::unique_ptr<Mmap> mmap(
      nt_string_view path) throw(IoError) override {
    return std::unique_ptr<Mmap>(new FileMmap(path));
  }

  Stat stat(const std::string &path) override {
    return genericStat(::stat, path);
  }

  Stat lstat(const std::string &path) override {
    return genericStat(::lstat, path);
  }

  void mkdir(nt_string_view path) throw(IoError) override {
    checkForMinusOne(::mkdir(NullterminatedString(path).c_str(), 0777));
  }

  void rmdir(nt_string_view path) throw(IoError) override {
    checkForMinusOne(::rmdir(NullterminatedString(path).c_str()));
  }

  void unlink(nt_string_view path) throw(IoError) override {
    checkForMinusOne(::unlink(NullterminatedString(path).c_str()));
  }

  void symlink(
      nt_string_view target,
      nt_string_view source) throw(IoError) override {
    checkForMinusOne(::symlink(
        NullterminatedString(target).c_str(),
        NullterminatedString(source).c_str()));
  }

  void rename(
      nt_string_view old_path,
      nt_string_view new_path) throw(IoError) override {
    checkForMinusOne(::rename(
        NullterminatedString(old_path).c_str(),
        NullterminatedString(new_path).c_str()));
  }

  void truncate(
      const std::string &path, size_t size) throw(IoError) override {
    checkForMinusOne(::truncate(path.c_str(), size));
  }

  std::vector<DirEntry> readDir(
      const std::string &path) throw(IoError) override {
    std::vector<DirEntry> result;

    DIR *dp = opendir(path.c_str());
    if (!dp) {
      throw IoError(strerror(errno), errno);
    }

    dirent *dptr;
    while (NULL != (dptr = readdir(dp))) {
      result.emplace_back(direntTypeToType(dptr->d_type), dptr->d_name);
    }
    closedir(dp);

    return result;
  }

  std::string readSymlink(const std::string &path) throw(IoError) override {
    std::vector<char> buf;
    int to_reserve = 128;

    int res = 0;
    for (;;) {
      buf.resize(to_reserve);
      res = readlink(path.c_str(), buf.data(), buf.capacity());
      if (res == to_reserve) {
        to_reserve *= 2;
      } else {
        break;
      }
    }

    if (res == -1) {
      throw IoError("Failed to read symlink", errno);
    }

    return buf.data();
  }

  std::string readFile(nt_string_view path) throw(IoError) override {
    std::string contents;
    processFile(path, [&contents](const char *buf, size_t len) {
      contents.append(buf, len);
    });
    return contents;
  }

  Hash hashFile(const std::string &path) throw(IoError) override {
    Hash hash;
    blake2b_state state;
    blake2b_init(&state, hash.data.size());
    processFile(path, [&state](const char *buf, size_t len) {
      blake2b_update(&state, reinterpret_cast<const uint8_t *>(buf), len);
    });
    blake2b_final(&state, hash.data.data(), hash.data.size());
    return hash;
  }

  std::string mkstemp(
      std::string &&filename_template) throw(IoError) override {
    const auto fd = ::mkstemp(&filename_template[0]);
    if (fd == -1) {
      throw IoError(
          std::string("Failed to create path for temporary file: ") +
          strerror(errno),
          errno);
    }
    close(fd);
    return filename_template;
  }

 private:
  static DirEntry::Type direntTypeToType(unsigned char type) {
    switch (type) {
    case DT_FIFO:
      return DirEntry::Type::FIFO;
    case DT_CHR:
      return DirEntry::Type::CHR;
    case DT_DIR:
      return DirEntry::Type::DIR;
    case DT_BLK:
      return DirEntry::Type::BLK;
    case DT_REG:
      return DirEntry::Type::REG;
    case DT_LNK:
      return DirEntry::Type::LNK;
    case DT_SOCK:
      return DirEntry::Type::SOCK;
    case DT_WHT:
      return DirEntry::Type::WHT;
    case DT_UNKNOWN:
    default:
      return DirEntry::Type::UNKNOWN;
    }
  }

  template<typename Append>
  void processFile(
      nt_string_view path,
      Append &&append) throw(IoError) {
#ifdef _WIN32
    // This makes a ninja run on a set of 1500 manifest files about 4% faster
    // than using the generic fopen code below.
    err->clear();
    HANDLE f = ::CreateFile(
        NullterminatedString(path).c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN,
        NULL);
    if (f == INVALID_HANDLE_VALUE) {
      throw IoError(GetLastErrorString(), ENOENT);
    }

    for (;;) {
      DWORD len;
      char buf[64 << 10];
      if (!::ReadFile(f, buf, sizeof(buf), &len, NULL)) {
        throw IoError(GetLastErrorString(), 0);
      }
      if (len == 0) {
        break;
      }
      append(buf, len);
    }
    ::CloseHandle(f);
#else
    FileHandle f(fopen(NullterminatedString(path).c_str(), "rb"));
    if (!f.get()) {
      throw IoError(strerror(errno), errno);
    }
    fcntl(fileno(f.get()), F_SETFD, FD_CLOEXEC);

    char buf[64 << 10];
    size_t len;
    while ((len = fread(buf, 1, sizeof(buf), f.get())) > 0) {
      append(buf, len);
    }
    if (ferror(f.get())) {
      const auto err = errno;
      throw IoError(strerror(err), err);
    }
#endif
  }

  template<typename StatFunction>
  Stat genericStat(StatFunction fn, const std::string &path) {
    Stat result;
    struct stat input;
    auto ret = fn(path.c_str(), &input);
    if (ret == -1) {
      result.result = errno;
    } else {
      result.result = 0;
      result.metadata.ino = input.st_ino;
      result.metadata.dev = input.st_dev;
      result.metadata.mode = input.st_mode;
      result.metadata.size = input.st_size;
      result.timestamps.mtime = input.st_mtime;
      result.timestamps.ctime = input.st_ctime;
    }
    return result;
  }
};

}  // anonymous namespace

std::unique_ptr<FileSystem> persistentFileSystem() {
  return std::unique_ptr<FileSystem>(new PersistentFileSystem());
}

}  // namespace shk
