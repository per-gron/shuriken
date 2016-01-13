#include "persistent_file_system.h"

#include <assert.h>
#include <errno.h>
#include <sys/stat.h>

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
    FileStream(const Path &path, const char *mode) throw(IoError) {
      _f = fopen(path.canonicalized().c_str(), mode);
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
  std::unique_ptr<Stream> open(const Path &path, const char *mode) throw(IoError) override {
    return std::unique_ptr<Stream>(new FileStream(path, mode));
  }

  Stat stat(const Path &path) override {
    return genericStat(::stat, path);
  }

  Stat lstat(const Path &path) override {
    return genericStat(::lstat, path);
  }

  void mkdir(const Path &path) throw(IoError) override {
    checkForMinusOne(::mkdir(path.canonicalized().c_str(), 0777));
  }

  void rmdir(const Path &path) throw(IoError) override {
    checkForMinusOne(::rmdir(path.canonicalized().c_str()));
  }

  void unlink(const Path &path) throw(IoError) override {
    checkForMinusOne(::unlink(path.canonicalized().c_str()));
  }

 private:
  template<typename StatFunction>
  Stat genericStat(StatFunction fn, const Path &path) {
    Stat result;
    struct stat input;
    auto ret = fn(path.canonicalized().c_str(), &input);
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
};

std::unique_ptr<FileSystem> persistentFileSystem() {
  return std::unique_ptr<FileSystem>(new PersistentFileSystem());
}

}  // namespace shk
