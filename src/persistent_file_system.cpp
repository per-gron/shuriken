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


#include "persistent_file_system.h"

#include <assert.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

namespace shk {

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
    FileStream(const std::string &path, const char *mode) throw(IoError) {
      _f = fopen(path.c_str(), mode);
      if (!_f) {
        throw IoError(strerror(errno), errno);
      }
    }

    virtual ~FileStream() {
      if (_f) {
        fclose(_f);
      }
    }

    size_t read(uint8_t *ptr, size_t size, size_t nitems) throw(IoError) override {
      auto result = fread(ptr, size, nitems, _f);
      if (eof()) {
        return result;
      } else if (ferror(_f) != 0) {
        throw IoError("Failed to read from stream", 0);
      } else {
        assert(result == nitems);
        return result;
      }
    }

    void write(const uint8_t *ptr, size_t size, size_t nitems) throw(IoError) override {
      fwrite(ptr, size, nitems, _f);
      if (ferror(_f) != 0) {
        throw IoError("Failed to write to stream", 0);
      }
    }

    long tell() const throw(IoError) override {
      return checkForMinusOne(ftell(_f));
    }

    bool eof() const override {
      return feof(_f) != 0;
    }

   private:
    FILE *_f = nullptr;
  };

 public:
  PersistentFileSystem(Paths &paths) : _paths(paths) {}

  Paths &paths() override {
    return _paths;
  }

  std::unique_ptr<Stream> open(
      const std::string &path, const char *mode) throw(IoError) override {
    return std::unique_ptr<Stream>(new FileStream(path, mode));
  }

  Stat stat(const std::string &path) override {
    return genericStat(::stat, path);
  }

  Stat lstat(const std::string &path) override {
    return genericStat(::lstat, path);
  }

  void mkdir(const std::string &path) throw(IoError) override {
    checkForMinusOne(::mkdir(path.c_str(), 0777));
  }

  void rmdir(const std::string &path) throw(IoError) override {
    checkForMinusOne(::rmdir(path.c_str()));
  }

  void unlink(const std::string &path) throw(IoError) override {
    checkForMinusOne(::unlink(path.c_str()));
  }

  std::string readFile(const std::string &path) throw(IoError) override {
    std::string contents;
#ifdef _WIN32
    // This makes a ninja run on a set of 1500 manifest files about 4% faster
    // than using the generic fopen code below.
    err->clear();
    HANDLE f = ::CreateFile(
        path.c_str(),
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
      contents.append(buf, len);
    }
    ::CloseHandle(f);
#else
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
      throw IoError(strerror(errno), errno);
    }

    char buf[64 << 10];
    size_t len;
    while ((len = fread(buf, 1, sizeof(buf), f)) > 0) {
      contents.append(buf, len);
    }
    if (ferror(f)) {
      fclose(f);
      throw IoError(strerror(errno), errno);  // XXX errno?
    }
    fclose(f);
#endif
    return contents;
  }

  std::string mkstemp(
      std::string &&filename_template) throw(IoError) override {
    if (::mkstemp(&filename_template[0]) == -1) {
      throw IoError(
          std::string("Failed to create path for temporary file: ") +
          strerror(errno),
          errno);
    }
    return filename_template;
  }

 private:
  template<typename StatFunction>
  Stat genericStat(StatFunction fn, const std::string &path) {
    Stat result;
    struct stat input;
    auto ret = fn(path.c_str(), &input);
    if (ret == -1) {
      result.result = ret;
    } else {
      result.result = 0;
      result.metadata.mode = input.st_mode;
      result.metadata.size = input.st_size;
      using time_point = std::chrono::system_clock::time_point;
      using duration = std::chrono::system_clock::duration;
      result.timestamps.mtime = time_point(duration(input.st_mtime));
      result.timestamps.ctime = time_point(duration(input.st_ctime));
    }
    return result;
  }

  Paths &_paths;
};

std::unique_ptr<FileSystem> persistentFileSystem(Paths &paths) {
  return std::unique_ptr<FileSystem>(new PersistentFileSystem(paths));
}

}  // namespace shk
