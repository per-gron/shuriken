#pragma once

#include <stdint.h>

#include <traceexec/traceexec_error.h>

/**
 * This header contains declarations for things that are private to the
 * traceexec userspace support library. It mainly exists for unit testing
 * purposes.
 *
 * The public interface is declared in the ../include/ directory.
 */

namespace traceexec {

struct Version {
  uint32_t major = 0;
  uint32_t minor = 0;
  uint32_t micro = 0;
};

/**
 * Get the version of the kernel extension. Throws TraceexecError if the version
 * extension is not loaded or if the version can't be retrieved for some other
 * reason.
 */
Version getKextVersion() throw(TraceexecError);

}
