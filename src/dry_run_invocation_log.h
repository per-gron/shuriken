#pragma once

#include "invocation_log.h"

namespace shk {

/**
 * An InvocationLog implementation that doesn't record any information.
 */
class DryRunInvocationLog : public InvocationLog {
 public:
  void createdDirectory(const std::string &path) throw(IoError) override {}
  void removedDirectory(const std::string &path) throw(IoError) override {}
  void ranCommand(
      const Hash &build_step_hash,
      std::vector<std::string> &&output_files,
      std::vector<std::string> &&input_files) throw(IoError) override {}
  void cleanedCommand(
      const Hash &build_step_hash) throw(IoError) override {}
};

}  // namespace shk
