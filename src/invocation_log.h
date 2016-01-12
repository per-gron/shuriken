#pragma once

#include "hash.h"
#include "invocations.h"
#include "io_error.h"
#include "path.h"

namespace shk {

class InvocationLog {
 public:
  virtual ~InvocationLog() = default;

  /**
   * Writes an entry in the invocation log that Shuriken has created a
   * directory. This will cause Shuriken to delete the directory in subsequent
   * invocations if it cleans up the last file of that directory.
   */
  virtual void createdDirectory(const Path &path) throw(IoError) = 0;

  /**
   * Writes an entry in the invocation log stating that Shuriken no longer is
   * responsible for the given directory. This should not be called unless the
   * given folder has been deleted in a cleanup process (or if it's gone).
   */
  virtual void removedDirectory(const Path &path) throw(IoError) = 0;

  /**
   * Writes an entry in the invocation log that says that the build step with
   * the given hash has been successfully run with information about outputs and
   * dependencies.
   */
  virtual void ranCommand(
      const Hash &build_step_hash,
      const Invocations::Entry &entry) throw(IoError) = 0;

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
