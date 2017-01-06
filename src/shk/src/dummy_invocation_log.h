#pragma once

#include "invocation_log.h"

namespace shk {

class DummyInvocationLog : public InvocationLog {
 public:
  void createdDirectory(const std::string &path) throw(IoError) {}

  void removedDirectory(const std::string &path) throw(IoError) {}

  void ranCommand(
      const Hash &build_step_hash,
      std::unordered_set<std::string> &&output_files,
      std::unordered_map<std::string, DependencyType> &&input_files)
          throw(IoError) {}

  void cleanedCommand(
      const Hash &build_step_hash) throw(IoError) {}
};

}  // namespace shk
