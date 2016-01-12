#pragma once

#include <chrono>

#include "io_error.h"
#include "path.h"

namespace shk {

struct FileMetadata {
  int mode = 0;
  size_t size = 0;
};

struct Timestamps {
  std::chrono::system_clock::time_point mtime;
  std::chrono::system_clock::time_point ctime;
};

struct Stat {
  /**
   * Return value of stat.
   */
  int result;
  FileMetadata metadata;
  Timestamps timestamps;
};

class FileSystem {
 public:
  virtual ~FileSystem() = default;

  class Stream {
   public:
    virtual ~Stream() = default;

    /**
     * Read ntitems objects, each size bytes long, storing them at the location
     * pointed to by ptr.
     * 
     * Returns the number of objects that were read. May be less if the end of
     * file was reached.
     */
    virtual size_t read(uint8_t *ptr, size_t size, size_t nitems) throw(IoError) = 0;
    /**
     * Write ntitems objects, each size bytes long, obtaining them from the
     * location given to by ptr.
     */
    virtual void write(const uint8_t *ptr, size_t size, size_t nitems) throw(IoError) = 0;
    virtual long tell() throw(IoError) = 0;
    virtual bool eof() = 0;
  };

  virtual std::unique_ptr<Stream> open(const Path &path, const char *mode) throw(IoError) = 0;
  virtual Stat stat(const Path &path) = 0;
  virtual Stat lstat(const Path &path) = 0;
  virtual void mkdir(const Path &path) throw(IoError) = 0;
  virtual void rmdir(const Path &path) throw(IoError) = 0;
  virtual void unlink(const Path &path) throw(IoError) = 0;
};

}  // namespace shk
