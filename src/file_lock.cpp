#include "file_lock.h"

#include <errno.h>
#include <sys/file.h>
#include <unistd.h>

namespace shk {

FileLock::FileLock(const std::string &path) throw(IoError)
    : _path(path),
      _f(fopen(path.c_str(), "w")) {
  if (!_f) {
    throw IoError(strerror(errno), errno);
  }
  if (flock(fileno(_f), LOCK_EX | LOCK_NB) != 0) {
    throw IoError(strerror(errno), errno);
  }
}

FileLock::~FileLock() {
  flock(fileno(_f), LOCK_UN);
  unlink(_path.c_str());
}

}  // namespace shk
