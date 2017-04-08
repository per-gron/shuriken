#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>

#include "clock.h"
#include "log/invocation_log.h"
#include "log/invocations.h"

namespace shk {

/**
 * An InvocationLog implementation that is memory backed rather than disk based
 * like the real InvocationLog. Used for testing and for dry runs.
 */
class InMemoryInvocationLog : public InvocationLog {
 public:
  struct Entry {
    std::vector<std::pair<std::string, Fingerprint>> output_files;
    std::vector<std::pair<std::string, Fingerprint>> input_files;
  };

  InMemoryInvocationLog(FileSystem &file_system, const Clock &clock);

  void createdDirectory(const std::string &path) throw(IoError) override;
  void removedDirectory(const std::string &path) throw(IoError) override;
  void ranCommand(
      const Hash &build_step_hash,
      std::unordered_set<std::string> &&output_files,
      std::unordered_set<std::string> &&input_files)
          throw(IoError) override;
  void cleanedCommand(
      const Hash &build_step_hash) throw(IoError) override;

  /**
   * Expose the contents of the in memory invocation log as an Invocations
   * object. This emulates what would happen if the invocation log would have
   * been read from disk.
   */
  Invocations invocations(Paths &paths) const;

  const std::unordered_set<std::string> &createdDirectories() const {
    return _created_directories;
  }

  const std::unordered_map<Hash, Entry> &entries() const {
    return _entries;
  }


 private:
  FileSystem &_fs;
  const Clock _clock;
  std::unordered_map<Hash, Entry> _entries;
  std::unordered_set<std::string> _created_directories;
};

}  // namespace shk
