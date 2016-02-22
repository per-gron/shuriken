#include "file_lock.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace shk {

FileLock::FileLock(const std::string &path) throw(IoError)
    : _path(path),
      _f(fopen(path.c_str(), "w")) {
  if (!_f.get()) {
    throw IoError(strerror(errno), errno);
  }
  const auto descriptor = fileno(_f.get());
  fcntl(descriptor, F_SETFD, FD_CLOEXEC);
  if (flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
    throw IoError(strerror(errno), errno);
  }
}

FileLock::~FileLock() {
  flock(fileno(_f.get()), LOCK_UN);
  unlink(_path.c_str());
}

}  // namespace shk
