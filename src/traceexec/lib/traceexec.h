#pragma once

#include <stdint.h>

#include <traceexec/traceexec.h>

/**
 * This header contains declarations for things that are private to the
 * traceexec userspace support library. It mainly exists for unit testing
 * purposes.
 *
 * The public interface is declared in the ../include/ directory.
 */

namespace traceexec {

struct Version {
  Version() = default;
  Version(uint32_t major, uint32_t minor, uint32_t micro)
      : major(major), minor(minor), micro(micro) {}

  uint32_t major = 0;
  uint32_t minor = 0;
  uint32_t micro = 0;

  /**
   * Following semver semantics, check if the version is compatible with a given
   * version. For example:
   *
   * Version({ 1, 2, 0 }).isCompatible(1, 0) == true
   * Version({ 1, 2, 0 }).isCompatible(1, 2) == true
   * Version({ 1, 2, 0 }).isCompatible(1, 3) == false
   * Version({ 1, 2, 0 }).isCompatible(2, 0) == false
   * Version({ 2, 0, 0 }).isCompatible(1, 0) == false
   */
  bool isCompatible(uint32_t major, uint32_t minor) const;
};

/**
 * Open a socket to the traceexec kernel extension, without doing a version
 * check or starting to trace the process.
 */
Socket openSocketNoVersionCheck() throw(TraceexecError);

/**
 * Get the version of the kernel extension. Throws TraceexecError if the version
 * extension is not loaded or if the version can't be retrieved for some other
 * reason.
 */
Version getKextVersion(const Socket &fd) throw(TraceexecError);

}
