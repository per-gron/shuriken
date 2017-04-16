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
      string_view path, const char *mode) throw(IoError) override {
    throw IoError("open not implemented for DryRunFileSystem", 0);
  }

  std::unique_ptr<Mmap> mmap(
      const std::string &path) throw(IoError) override {
    return _inner.mmap(path);
  }

  Stat stat(const std::string &path) override {
    return _inner.stat(path);
  }

  Stat lstat(const std::string &path) override {
    return _inner.lstat(path);
  }

  void mkdir(const std::string &path) throw(IoError) override {}

  void rmdir(const std::string &path) throw(IoError) override {}

  void unlink(const std::string &path) throw(IoError) override {}

  void symlink(
      const std::string &target,
      const std::string &source) throw(IoError) override {}

  void rename(
      const std::string &old_path,
      const std::string &new_path) throw(IoError) override {}

  void truncate(
      const std::string &path, size_t size) throw(IoError) override {}

  std::vector<DirEntry> readDir(
      const std::string &path) throw(IoError) override {
    return _inner.readDir(path);
  }

  std::string readSymlink(const std::string &path) throw(IoError) override {
    return _inner.readSymlink(path);
  }

  std::string readFile(const std::string &path) throw(IoError) override {
    return _inner.readFile(path);
  }

  Hash hashFile(const std::string &path) throw(IoError) override {
    return _inner.hashFile(path);
  }

  std::string mkstemp(
      std::string &&filename_template) throw(IoError) override {
    return "";
  }

 private:
  FileSystem &_inner;
};

}  // anonymous namespace

std::unique_ptr<FileSystem> dryRunFileSystem(FileSystem &inner_file_system) {
  return std::unique_ptr<FileSystem>(new DryRunFileSystem(inner_file_system));
}

}  // namespace shk
