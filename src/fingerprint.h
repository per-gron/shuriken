#pragma once

#include <functional>
#include <string>

#include "file_system.h"
#include "hash.h"

namespace shk {

struct Fingerprint {
  Stat stat;
  Hash hash;
  std::chrono::system_clock::time_point timestamp;
  std::string path;
};

struct MatchesResult {
  bool matches = false;
  bool should_update = false;
};

MatchesResult fingerprintMatches(
    const Fingerprint &fingerprint,
    const std::function<Stat (const std::string &path)> stat,
    const std::function<Hash (const std::string &path)> hash);

}  // namespace shk
