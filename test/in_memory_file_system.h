#include "file_system.h"

#include <unordered_map>
#include <unordered_set>

namespace shk {

/**
 * FileSystem that is backed only by memory. Used for testing. In addition to
 * the FileSystem functionality, it is also copiable and offers an equality
 * operator, which is useful to see if a sequence of operations produce
 * identical results.
 *
 * Does not support absolute paths.
 */
class InMemoryFileSystem : public FileSystem {
 public:
  InMemoryFileSystem();

  std::unique_ptr<Stream> open(
      const std::string &path, const char *mode) throw(IoError) override;
  Stat stat(const std::string &path) override;
  Stat lstat(const std::string &path) override;
  void mkdir(const std::string &path) throw(IoError) override;
  void rmdir(const std::string &path) throw(IoError) override;
  void unlink(const std::string &path) throw(IoError) override;
  std::string readFile(const std::string &path) throw(IoError) override;
  std::string mkstemp(std::string &&filename_template) throw(IoError) override;

  bool operator==(const InMemoryFileSystem &other) const;

 private:
  enum class EntryType {
    FILE_DOES_NOT_EXIST,
    DIRECTORY_DOES_NOT_EXIST,
    DIRECTORY,
    FILE,
  };

  struct File {
    File(ino_t ino) : ino(ino) {}

    ino_t ino;
    std::string contents;
  };

  struct Directory {
    Directory(ino_t ino) : ino(ino) {}

    ino_t ino;

    /**
     * Key is the basename of the file, value is the contents of the file. It's
     * a shared pointer to make it possible to keep a stream to it open even
     * after unlinking it.
     */
    std::unordered_map<std::string, std::shared_ptr<File>> files;

    std::unordered_set<std::string> directories;

    bool empty() const;
    bool operator==(const Directory &other) const;
  };

  struct LookupResult {
    EntryType entry_type = EntryType::FILE_DOES_NOT_EXIST;
    Directory *directory = nullptr;
    std::string basename;
  };

  class InMemoryFileStream : public Stream {
   public:
    InMemoryFileStream(
        const std::shared_ptr<File> &file,
        bool read,
        bool write);

    size_t read(
        uint8_t *ptr, size_t size, size_t nitems) throw(IoError) override;
    void write(
        const uint8_t *ptr, size_t size, size_t nitems) throw(IoError) override;
    long tell() const throw(IoError) override;
    bool eof() const override;

   private:
    void checkNotEof() const throw(IoError);

    const bool _read;
    const bool _write;
    bool _eof = false;
    size_t _position = 0;
    const std::shared_ptr<File> _file;
  };

  LookupResult lookup(const std::string &path);

  std::unordered_map<std::string, Directory> _directories;
  ino_t _ino = 0;
};

/**
 * Helper function for writing a string to a file.
 */
void writeFile(
    FileSystem &file_system,
    const std::string &path,
    const std::string &contents) throw(IoError);

/**
 * Create directory and parent directories. Like mkdir -p
 */
void mkdirs(FileSystem &file_system, const std::string &path) throw(IoError);

/**
 * Make sure that there is a directory for the given path. Like
 * mkdir -p `dirname path`
 */
void mkdirsFor(FileSystem &file_system, const std::string &path) throw(IoError);

}  // namespace shk
