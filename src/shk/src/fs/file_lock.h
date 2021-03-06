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

#pragma once

#include <cstdio>
#include <stdexcept>
#include <string>

#include <util/raii_helper.h>
#include <util/string_view.h>

#include "io_error.h"

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
