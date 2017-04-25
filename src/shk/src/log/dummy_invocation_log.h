#pragma once

#include "log/invocation_log.h"

namespace shk {

class DummyInvocationLog : public InvocationLog {
 public:
  void createdDirectory(nt_string_view path)
      throw(IoError) override {}

  void removedDirectory(nt_string_view path)
      throw(IoError) override {}

  std::pair<Fingerprint, FileId> fingerprint(const std::string &path) override {
    return std::make_pair(Fingerprint(), FileId());
  }

  void ranCommand(
      const Hash &build_step_hash,
      std::vector<std::string> &&output_files,
      std::vector<Fingerprint> &&output_fingerprints,
      std::vector<std::string> &&input_files,
      std::vector<Fingerprint> &&input_fingerprints)
          throw(IoError) override {}

  void cleanedCommand(
      const Hash &build_step_hash) throw(IoError) override {}
};

}  // namespace shk
