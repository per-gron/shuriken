// Copyright 2011 Google Inc. All Rights Reserved.
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

  std::unique_ptr<Mmap> mmap(
      nt_string_view path) throw(IoError) override {
    return _inner.mmap(path);
  }

  Stat stat(nt_string_view path) override {
    return _inner.stat(path);
  }

  Stat lstat(nt_string_view path) override {
    return _inner.lstat(path);
  }

  void mkdir(nt_string_view path) throw(IoError) override {}

  void rmdir(nt_string_view path) throw(IoError) override {}

  void unlink(nt_string_view path) throw(IoError) override {}

  void symlink(
      nt_string_view target,
      nt_string_view source) throw(IoError) override {}

  void rename(
      nt_string_view old_path,
      nt_string_view new_path) throw(IoError) override {}

  void truncate(
      nt_string_view path, size_t size) throw(IoError) override {}

  std::vector<DirEntry> readDir(
      nt_string_view path) throw(IoError) override {
    return _inner.readDir(path);
  }

  std::pair<std::string, bool> readSymlink(
      nt_string_view path, std::string *err) override {
    return _inner.readSymlink(path, err);
  }

  std::string readFile(nt_string_view path) throw(IoError) override {
    return _inner.readFile(path);
  }

  std::pair<Hash, bool> hashFile(
      nt_string_view path, std::string *err) override {
    return _inner.hashFile(path, err);
  }

  std::pair<std::string, bool> mkstemp(
      std::string &&filename_template, std::string *err) override {
    return std::make_pair(std::string(""), true);
  }

 private:
  FileSystem &_inner;
};

}  // anonymous namespace

std::unique_ptr<FileSystem> dryRunFileSystem(FileSystem &inner_file_system) {
  return std::unique_ptr<FileSystem>(new DryRunFileSystem(inner_file_system));
}

}  // namespace shk
