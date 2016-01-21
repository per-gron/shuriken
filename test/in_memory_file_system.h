#include "file_system.h"

#include <unordered_map>

namespace shk {
namespace detail {

/**
 * Split a path into its dirname and basename. The first element in the
 * pair is the dirname, the second the basename.
 *
 * Does not support absolute paths.
 */
std::pair<std::string, std::string> basenameSplit(const std::string &path);

std::string dirname(const std::string &path);

}  // namespace detail

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
  InMemoryFileSystem(Paths &paths);

  Paths &paths() override;
  std::unique_ptr<Stream> open(const Path &path, const char *mode) throw(IoError) override;
  Stat stat(const Path &path) override;
  Stat lstat(const Path &path) override;
  void mkdir(const Path &path) throw(IoError) override;
  void rmdir(const Path &path) throw(IoError) override;
  void unlink(const Path &path) throw(IoError) override;
  std::string readFile(const Path &path) throw(IoError) override;
  Path mkstemp(std::string &&filename_template) throw(IoError) override;

  bool operator==(const InMemoryFileSystem &other) const;

 private:
  enum class EntryType {
    FILE_DOES_NOT_EXIST,
    DIRECTORY_DOES_NOT_EXIST,
    DIRECTORY,
    FILE,
  };

  struct File {
    std::string contents;
  };

  struct Directory {
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

  LookupResult lookup(const Path &path);

  Paths *_paths;
  std::unordered_map<Path, Directory> _directories;
};

/**
 * Helper function for writing a string to a file.
 */
void writeFile(
    FileSystem &file_system,
    const Path &path,
    const std::string &contents) throw(IoError);

/**
 * Create directory and parent directories. Like mkdir -p
 */
void mkdirs(FileSystem &file_system, const Path &path) throw(IoError);

/**
 * Make sure that there is a directory for the given path. Like
 * mkdir -p `dirname path`
 */
void mkdirsFor(FileSystem &file_system, const Path &path) throw(IoError);

}  // namespace shk
