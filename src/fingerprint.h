#pragma once

#include <functional>
#include <string>

namespace shk {

struct FileMetadata {
  int mode = 0;
  size_t size = 0;
};

struct Timestamps {
  Timestamp mtime = 0;
  Timestamp ctime = 0;
};

struct Stat {
  FileMetadata metadata;
  Timestamps timestamps;
};

struct Fingerprint {
  Stat stat;
  Hash hash = 0;
  Timestamp timestamp;
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
