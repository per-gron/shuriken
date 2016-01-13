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

  std::unique_ptr<Stream> open(const Path &path, const char *mode) throw(IoError) override;
  Stat stat(const Path &path) override;
  Stat lstat(const Path &path) override;
  void mkdir(const Path &path) throw(IoError) override;
  void rmdir(const Path &path) throw(IoError) override;
  void unlink(const Path &path) throw(IoError) override;

  bool operator==(const InMemoryFileSystem &other) const;

 private:
  enum class EntryType {
    FILE_DOES_NOT_EXIST,
    DIRECTORY_DOES_NOT_EXIST,
    DIRECTORY,
    FILE,
  };

  struct Directory {
    /**
     * Key is the basename of the file, value is the contents of the file. It's
     * a shared pointer to make it possible to keep a stream to it open even
     * after unlinking it.
     */
    std::unordered_map<std::string, std::shared_ptr<std::string>> files;

    std::unordered_set<std::string> directories;

    bool empty() const;
    bool operator==(const Directory &other) const;
  };

  struct LookupResult {
    EntryType entry_type = EntryType::FILE_DOES_NOT_EXIST;
    Directory *directory = nullptr;
    std::string basename;
  };

  LookupResult lookup(const Path &path);

  Paths *_paths;
  std::unordered_map<Path, Directory> _directories;
};

}  // namespace shk
