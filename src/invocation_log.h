#pragma once

#include <string>
#include <vector>

#include "fingerprint.h"
#include "hash.h"
#include "io_error.h"
#include "path.h"

namespace shk {

/**
 * InvocationLog is a class that is used during a build to manipulate the
 * on-disk storage of the invocation log. It does not offer means to read
 * Invocations from the invocation log; that is done in a separate build step
 * so it is done separately.
 */
class InvocationLog {
 public:
  struct Entry {
    std::vector<std::pair<std::string, Fingerprint>> output_files;
    std::vector<std::pair<std::string, Fingerprint>> input_files;
  };

  virtual ~InvocationLog() = default;

  /**
   * Writes an entry in the invocation log that Shuriken has created a
   * directory. This will cause Shuriken to delete the directory in subsequent
   * invocations if it cleans up the last file of that directory.
   */
  virtual void createdDirectory(const std::string &path) throw(IoError) = 0;

  /**
   * Writes an entry in the invocation log stating that Shuriken no longer is
   * responsible for the given directory. This should not be called unless the
   * given folder has been deleted in a cleanup process (or if it's gone).
   */
  virtual void removedDirectory(const std::string &path) throw(IoError) = 0;

  /**
   * Writes an entry in the invocation log that says that the build step with
   * the given hash has been successfully run with information about outputs and
   * dependencies.
   */
  virtual void ranCommand(
      const Hash &build_step_hash,
      const Entry &entry) throw(IoError) = 0;

  /**
   * Writes an entry in the invocation log that says that the build step with
   * the given hash has been cleaned and can be treated as if it was never run.
   *
   * It is the responsibility of the caller to ensure that all output files are
   * actually cleaned before calling this method.
   */
  virtual void cleanedCommand(
      const Hash &build_step_hash) throw(IoError) = 0;
};

}  // namespace shk
