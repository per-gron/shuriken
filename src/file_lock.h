#pragma once

#include <cstdio>
#include <stdexcept>
#include <string>

#include "io_error.h"
#include "raii_helper.h"

namespace shk {

class FileLock {
 public:
  FileLock(const std::string &path) throw(IoError);
  ~FileLock();

 private:
  const std::string _path;
  using FileHandle = RAIIHelper<FILE, int, fclose>;
  const FileHandle _f;
};

}  // namespace shk
