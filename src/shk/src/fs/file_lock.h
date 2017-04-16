#pragma once

#include <cstdio>
#include <stdexcept>
#include <string>

#include <util/raii_helper.h>

#include "io_error.h"
#include "string_view.h"

namespace shk {

class FileLock {
 public:
  FileLock(nt_string_view path) throw(IoError);
  ~FileLock();

 private:
  const std::string _path;
  using FileHandle = RAIIHelper<FILE *, int, fclose>;
  const FileHandle _f;
};

}  // namespace shk
