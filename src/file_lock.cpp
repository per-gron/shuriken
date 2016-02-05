#include "file_lock.h"

#include <errno.h>
#include <sys/file.h>

namespace shk {

FileLock::FileLock(const std::string &path) throw(IoError)
    : f_(fopen(path.c_str(), "w")) {
  if (!f_) {
    throw IoError(strerror(errno), errno);
  }
  if (flock(fileno(f_), LOCK_EX | LOCK_NB) != 0) {
    throw IoError(strerror(errno), errno);
  }
}

FileLock::~FileLock() {
  flock(fileno(f_), LOCK_UN);
}

}  // namespace shk
