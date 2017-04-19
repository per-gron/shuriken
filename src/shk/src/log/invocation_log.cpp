#include "log/invocation_log.h"

namespace shk {

std::vector<Fingerprint> InvocationLog::fingerprintFiles(
    const std::vector<std::string> &files) {
  std::vector<Fingerprint> ans;
  ans.reserve(files.size());
  for (const auto &file : files) {
    ans.push_back(fingerprint(file).first);
  }
  return ans;
}

}  // namespace shk
