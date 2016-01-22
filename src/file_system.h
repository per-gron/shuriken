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
  // TODO(peck): What should ctime be used for exactly?
  std::chrono::system_clock::time_point ctime;
};

struct Stat {
  /**
   * Return value of stat.
   */
  int result = 0;
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
    virtual size_t read(
        uint8_t *ptr, size_t size, size_t nitems) throw(IoError) = 0;
    /**
     * Write ntitems objects, each size bytes long, obtaining them from the
     * location given to by ptr.
     */
    virtual void write(
        const uint8_t *ptr, size_t size, size_t nitems) throw(IoError) = 0;
    virtual long tell() const throw(IoError) = 0;
    virtual bool eof() const = 0;
  };

  /**
   * Return the Paths object that this FileSystem uses for its paths.
   */
  virtual Paths &paths() = 0;

  virtual std::unique_ptr<Stream> open(
      const Path &path, const char *mode) throw(IoError) = 0;
  virtual Stat stat(const Path &path) = 0;
  virtual Stat lstat(const Path &path) = 0;
  virtual void mkdir(const Path &path) throw(IoError) = 0;
  virtual void rmdir(const Path &path) throw(IoError) = 0;
  virtual void unlink(const Path &path) throw(IoError) = 0;

  /**
   * Utility function for reading files. It is on this interface because on
   * Windows reading the file as a whole is faster than reading it using Stream.
   */
  virtual std::string readFile(const Path &path) throw(IoError) = 0;

  /**
   * Create a temporary file that follows a template. See the man page for
   * mkstemp. This is necessary to have on the FileSystem interface for the
   * same reason mkstemp exists: mktemp that creates a temporary file path
   * often creates races when used, because it is possible that others will
   * create a file at that path between mktemp returns and when the file is
   * first created. mkstemp chooses a path and creates a file atomically,
   * avoiding this problem.
   */
  virtual std::string mkstemp(
      std::string &&filename_template) throw(IoError) = 0;
};

}  // namespace shk
