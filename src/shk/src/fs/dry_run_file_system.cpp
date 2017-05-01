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

#include "fs/dry_run_file_system.h"

namespace shk {
namespace {

class DryRunFileSystem : public FileSystem {
 public:
  DryRunFileSystem(FileSystem &inner_file_system)
      : _inner(inner_file_system) {}

  std::unique_ptr<Stream> open(
      nt_string_view path, const char *mode) throw(IoError) override {
    throw IoError("open not implemented for DryRunFileSystem", 0);
  }

  USE_RESULT std::pair<std::unique_ptr<Mmap>, IoError> mmap(
      nt_string_view path) override {
    return _inner.mmap(path);
  }

  Stat stat(nt_string_view path) override {
    return _inner.stat(path);
  }

  Stat lstat(nt_string_view path) override {
    return _inner.lstat(path);
  }

  USE_RESULT IoError mkdir(nt_string_view path) override {
    return IoError::success();
  }

  USE_RESULT IoError rmdir(nt_string_view path) override {
    return IoError::success();
  }

  USE_RESULT IoError unlink(nt_string_view path) override {
    return IoError::success();
  }

  USE_RESULT IoError symlink(
      nt_string_view target, nt_string_view source) override {
    return IoError::success();
  }

  USE_RESULT IoError rename(
      nt_string_view old_path,
      nt_string_view new_path) override {
    return IoError::success();
  }

  USE_RESULT IoError truncate(nt_string_view path, size_t size) override {
    return IoError::success();
  }

  USE_RESULT std::pair<std::vector<DirEntry>, IoError> readDir(
      nt_string_view path) override {
    return _inner.readDir(path);
  }

  USE_RESULT std::pair<std::string, IoError> readSymlink(
      nt_string_view path) override {
    return _inner.readSymlink(path);
  }

  USE_RESULT std::pair<std::string, IoError> readFile(
      nt_string_view path) override {
    return _inner.readFile(path);
  }

  USE_RESULT std::pair<Hash, IoError> hashFile(
      nt_string_view path) override {
    return _inner.hashFile(path);
  }

  USE_RESULT std::pair<std::string, IoError> mkstemp(
      std::string &&filename_template) override {
    return std::make_pair(std::string(""), IoError::success());
  }

 private:
  FileSystem &_inner;
};

}  // anonymous namespace

std::unique_ptr<FileSystem> dryRunFileSystem(FileSystem &inner_file_system) {
  return std::unique_ptr<FileSystem>(new DryRunFileSystem(inner_file_system));
}

}  // namespace shk
