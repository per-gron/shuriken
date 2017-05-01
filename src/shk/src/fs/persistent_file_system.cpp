// Copyright 2011 Google Inc. All Rights Reserved.
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
  static T checkForMinusOne(T result) throw(IoError) {
    if (result == -1) {
      throw IoError(strerror(errno), errno);
    } else {
      return result;
    }
  }

  template<typename T>
  static IoError checkForMinusOneIoError(T result) {
    if (result == -1) {
      return IoError(strerror(errno), errno);
    } else {
      return IoError::success();
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

    size_t read(
        uint8_t *ptr, size_t size, size_t nitems) throw(IoError) override {
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

    USE_RESULT IoError write(
        const uint8_t *ptr, size_t size, size_t nitems) override {
      fwrite(ptr, size, nitems, _f.get());
      if (ferror(_f.get()) != 0) {
        return IoError("Failed to write to stream", 0);
      }
      return IoError::success();
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

  Stat stat(nt_string_view path) override {
    return genericStat(::stat, path);
  }

  Stat lstat(nt_string_view path) override {
    return genericStat(::lstat, path);
  }

  USE_RESULT IoError mkdir(nt_string_view path) override {
    return checkForMinusOneIoError(
        ::mkdir(NullterminatedString(path).c_str(), 0777));
  }

  USE_RESULT IoError rmdir(nt_string_view path) override {
    return checkForMinusOneIoError(
        ::rmdir(NullterminatedString(path).c_str()));
  }

  USE_RESULT IoError unlink(nt_string_view path) override {
    return checkForMinusOneIoError(
        ::unlink(NullterminatedString(path).c_str()));
  }

  USE_RESULT IoError symlink(
      nt_string_view target,
      nt_string_view source) override {
    return checkForMinusOneIoError(
        ::symlink(
            NullterminatedString(target).c_str(),
            NullterminatedString(source).c_str()));
  }

  USE_RESULT IoError rename(
      nt_string_view old_path,
      nt_string_view new_path) override {
    return checkForMinusOneIoError(::rename(
        NullterminatedString(old_path).c_str(),
        NullterminatedString(new_path).c_str()));
  }

  USE_RESULT IoError truncate(nt_string_view path, size_t size) override {
    return checkForMinusOneIoError(
        ::truncate(NullterminatedString(path).c_str(), size));
  }

  USE_RESULT std::pair<std::vector<DirEntry>, IoError> readDir(
      nt_string_view path) override {
    std::vector<DirEntry> result;

    DIR *dp = opendir(NullterminatedString(path).c_str());
    if (!dp) {
      return std::make_pair(
          std::vector<DirEntry>(),
          IoError(strerror(errno), errno));
    }

    dirent *dptr;
    while (NULL != (dptr = readdir(dp))) {
      result.emplace_back(direntTypeToType(dptr->d_type), dptr->d_name);
    }
    closedir(dp);

    return std::make_pair(std::move(result), IoError::success());
  }

  USE_RESULT std::pair<std::string, IoError> readSymlink(
      nt_string_view path) override {
    std::vector<char> buf;
    int to_reserve = 128;

    int res = 0;
    for (;;) {
      buf.resize(to_reserve);
      res = readlink(
          NullterminatedString(path).c_str(),
          buf.data(),
          buf.capacity());
      if (res == to_reserve) {
        to_reserve *= 2;
      } else {
        break;
      }
    }

    if (res == -1) {
      return std::make_pair("", IoError("Failed to read symlink", 0));
    }

    return std::make_pair(buf.data(), IoError::success());
  }

  USE_RESULT std::pair<std::string, IoError> readFile(
      nt_string_view path) override {
    try {
      const auto file_stat = stat(path);
      std::string contents;
      contents.reserve(file_stat.metadata.size);
      processFile(path, [&contents](const char *buf, size_t len) {
        contents.append(buf, len);
      });
      return std::make_pair(std::move(contents), IoError::success());
    } catch (const IoError &io_error) {
      return std::make_pair(std::string(), io_error);
    }
  }

  std::pair<Hash, bool> hashFile(
      nt_string_view path, std::string *err) override {
    Hash hash;
    blake2b_state state;
    blake2b_init(&state, hash.data.size());
    try {
      processFile(path, [&state](const char *buf, size_t len) {
        blake2b_update(&state, reinterpret_cast<const uint8_t *>(buf), len);
      });
    } catch (const IoError &io_error) {
      *err = io_error.what();
      return std::make_pair(Hash(), false);
    }
    blake2b_final(&state, hash.data.data(), hash.data.size());
    return std::make_pair(hash, true);
  }

  USE_RESULT std::pair<std::string, IoError> mkstemp(
      std::string &&filename_template) override {
    const auto fd = ::mkstemp(&filename_template[0]);
    if (fd == -1) {
      return std::make_pair(
          std::string(""),
          IoError(
              std::string("Failed to create path for temporary file: ") +
                  strerror(errno),
              errno));
    }
    close(fd);
    return std::make_pair(filename_template, IoError::success());
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
  Stat genericStat(StatFunction fn, nt_string_view path) {
    Stat result;
    struct stat input;
    auto ret = fn(NullterminatedString(path).c_str(), &input);
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
