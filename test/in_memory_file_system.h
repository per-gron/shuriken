#include "file_system.h"

#include <unordered_map>

namespace shk {

/**
 * FileSystem that is backed only by memory. Used for testing. In addition to
 * the FileSystem functionality, it is also copiable and offers an equality
 * operator, which is useful to see if a sequence of operations produce
 * identical results.
 */
class InMemoryFileSystem : public FileSystem {
 public:
  std::unique_ptr<Stream> open(const Path &path, const char *mode) throw(IoError) override;
  Stat stat(const Path &path) override;
  Stat lstat(const Path &path) override;
  void mkdir(const Path &path) throw(IoError) override;
  void rmdir(const Path &path) throw(IoError) override;
  void unlink(const Path &path) throw(IoError) override;

 private:
  struct Directory {
    /**
     * Key is the basename of the file, value is the contents of the file.
     */
    std::unordered_map<std::string, std::string> files;
  };

  std::unordered_map<Path, Directory> _directories;
};

bool operator==(const InMemoryFileSystem &a, const InMemoryFileSystem &b);

}  // namespace shk
