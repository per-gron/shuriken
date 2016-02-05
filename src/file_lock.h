#pragma once

#include <cstdio>
#include <stdexcept>
#include <string>

#include "io_error.h"

namespace shk {

class FileLock {
 public:
  FileLock(const std::string &path) throw(IoError);
  ~FileLock();

 private:
  FILE * const f_;
};

}  // namespace shk
