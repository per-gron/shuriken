#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include "fs/fingerprint.h"
#include "fs/path.h"
#include "hash.h"
#include "io_error.h"

namespace shk {

/**
 * InvocationLog is a class that is used during a build to manipulate the
 * on-disk storage of the invocation log. It does not offer means to read
 * Invocations from the invocation log; that is done in a separate build step
 * so it is done separately.
 */
class InvocationLog {
 public:
  virtual ~InvocationLog() = default;

  /**
   * Writes an entry in the invocation log that Shuriken has created a
   * directory. This will cause Shuriken to delete the directory in subsequent
   * invocations if it cleans up the last file of that directory.
   *
   * It is recommended to only provide normalized paths to this method. For
   * an explanation why, see removedDirectory.
   */
  virtual void createdDirectory(const std::string &path) throw(IoError) = 0;

  /**
   * Writes an entry in the invocation log stating that Shuriken no longer is
   * responsible for the given directory. This should not be called unless the
   * given folder has been deleted in a cleanup process (or if it's gone).
   *
   * This method does not have any intelligence when it comes to paths; the
   * provided path must be byte equal to the path that was previously provided
   * to createdDirectory. For this reason it is recommended to only give
   * normalized paths to this method and createdDirectory.
   */
  virtual void removedDirectory(const std::string &path) throw(IoError) = 0;

  /**
   * Writes an entry in the invocation log that says that the build step with
   * the given hash has been successfully run with information about outputs and
   * dependencies.
   *
   * The InvocationLog will fingerprint the provided paths, reusing existing
   * fingerprints if possible.
   *
   * Output files that are directories are treated the same as calling
   * createdDirectory. For more info, see Invocations::created_directories.
   */
  virtual void ranCommand(
      const Hash &build_step_hash,
      std::vector<std::string> &&output_files,
      std::vector<std::string> &&input_files)
          throw(IoError) = 0;

  /**
   * Helper function that is useful when rewriting an already existing
   * invocation log entry, for example when recompacting or when rewriting it
   * because it is racily clean.
   *
   * The fingerprints parameter has the same format and purpose as
   * Invocations::fingerprints. The output_files and input_files vectors contain
   * indices into this array.
   */
  void relogCommand(
      const Hash &build_step_hash,
      const std::vector<std::pair<Path, Fingerprint>> &fingerprints,
      const std::vector<size_t> &output_files,
      const std::vector<size_t> &input_files);

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
