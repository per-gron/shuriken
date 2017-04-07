#include "fs/file_system.h"

#include <deque>
#include <unordered_map>
#include <unordered_set>

namespace shk {

/**
 * FileSystem that is backed only by memory. Used for testing. In addition to
 * the FileSystem functionality, it is also copiable and offers an equality
 * operator, which is useful to see if a sequence of operations produce
 * identical results.
 */
class InMemoryFileSystem : public FileSystem {
 public:
  InMemoryFileSystem(const std::function<time_t ()> &clock = []{ return 0; });

  /**
   * Causes the next mkstemp to return path. Can be useful in tests that need to
   * predict temporary paths.
   */
  void enqueueMkstempResult(std::string &&path);

  std::unique_ptr<Stream> open(
      const std::string &path, const char *mode) throw(IoError) override;
  std::unique_ptr<Mmap> mmap(
      const std::string &path) throw(IoError) override;
  Stat stat(const std::string &path) override;
  Stat lstat(const std::string &path) override;
  void mkdir(const std::string &path) throw(IoError) override;
  void rmdir(const std::string &path) throw(IoError) override;
  void unlink(const std::string &path) throw(IoError) override;
  void symlink(
      const std::string &target,
      const std::string &source) throw(IoError) override;
  void rename(
      const std::string &old_path,
      const std::string &new_path) throw(IoError) override;
  void truncate(
      const std::string &path, size_t size) throw(IoError) override;
  std::vector<DirEntry> readDir(
      const std::string &path) throw(IoError) override;
  std::string readSymlink(
      const std::string &path) throw(IoError) override;
  std::string readFile(const std::string &path) throw(IoError) override;
  Hash hashFile(const std::string &path) throw(IoError) override;
  std::string mkstemp(std::string &&filename_template) throw(IoError) override;

  bool operator==(const InMemoryFileSystem &other) const;

 private:
  std::unique_ptr<Stream> open(
      bool expect_symlink,
      const std::string &path,
      const char *mode) throw(IoError);

  Stat stat(bool follow_symlink, const std::string &path);

  enum class EntryType {
    FILE_DOES_NOT_EXIST,
    DIRECTORY_DOES_NOT_EXIST,
    DIRECTORY,
    FILE,
  };

  struct File {
    File(ino_t ino) : ino(ino) {}

    time_t mtime = 0;
    ino_t ino;
    std::string contents;
    bool symlink = false;
  };

  struct Directory {
    Directory() = default;
    Directory(time_t mtime, ino_t ino) :
        mtime(mtime), ino(ino) {}
    Directory(const Directory &other) = default;
    Directory(Directory &&other) = default;
    Directory &operator=(const Directory &other) = default;
    Directory &operator=(Directory &&other) = default;

    time_t mtime = 0;
    ino_t ino = 0;

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
    std::string canonicalized;
  };

  class InMemoryFileStream : public Stream {
   public:
    InMemoryFileStream(
        const std::function<time_t ()> &clock,
        const std::shared_ptr<File> &file,
        bool read,
        bool write,
        bool append);

    size_t read(
        uint8_t *ptr, size_t size, size_t nitems) throw(IoError) override;
    void write(
        const uint8_t *ptr, size_t size, size_t nitems) throw(IoError) override;
    long tell() const throw(IoError) override;
    bool eof() const override;

   private:
    void checkNotEof() const throw(IoError);

    const std::function<time_t ()> _clock;
    const bool _read;
    const bool _write;
    bool _eof = false;
    size_t _position;
    const std::shared_ptr<File> _file;
  };

  class InMemoryMmap : public Mmap {
   public:
    InMemoryMmap(const std::shared_ptr<File> &file);

    StringPiece memory() override;

   private:
    const std::shared_ptr<File> _file;
  };

  LookupResult lookup(const std::string &path);

  std::deque<std::string> _mkstemp_paths;
  const std::function<time_t ()> _clock;
  std::unordered_map<std::string, Directory> _directories;
  ino_t _ino = 0;
};

}  // namespace shk
