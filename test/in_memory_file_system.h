#include "file_system.h"

namespace shk {

class InMemoryFileSystem : public FileSystem {
 public:
  std::unique_ptr<Stream> open(const Path &path, const char *mode) throw(IoError) override;
  Stat stat(const Path &path) override;
  Stat lstat(const Path &path) override;
  void mkdir(const Path &path) throw(IoError) override;
  void rmdir(const Path &path) throw(IoError) override;
  void unlink(const Path &path) throw(IoError) override;
};

bool operator==(const InMemoryFileSystem &a, const InMemoryFileSystem &b);

}  // namespace shk
