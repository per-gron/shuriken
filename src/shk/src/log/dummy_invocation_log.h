#pragma once

#include "log/invocation_log.h"

namespace shk {

class DummyInvocationLog : public InvocationLog {
 public:
  void createdDirectory(const std::string &path)
      throw(IoError) override {}

  void removedDirectory(const std::string &path)
      throw(IoError) override {}

  void ranCommand(
      const Hash &build_step_hash,
      std::unordered_set<std::string> &&output_files,
      std::unordered_set<std::string> &&input_files)
          throw(IoError) override {}

  void cleanedCommand(
      const Hash &build_step_hash) throw(IoError) override {}
};

}  // namespace shk
