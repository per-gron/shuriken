// Copyright 2017 Per Grön. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "fs/file_lock.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace shk {

FileLock::FileLock(nt_string_view path) throw(IoError)
    : _path(path),
      _f(fopen(_path.c_str(), "w")) {
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
