#pragma once

#include <traceexec/traceexec_error.h>

#include <util/raii_helper.h>

namespace traceexec {

using TraceexecSocket = util::RAIIHelper<int, int, close, -1>;

/**
 * Open a socket to the traceexec kernel extension and start tracing the current
 * process.
 *
 * Throws TraceexecError if the kernel extension is not loaded, if the kernel
 * extension's version is not compatible with this library or if the operation
 * fails for some other reason.
 */
TraceexecSocket openSocket() throw(TraceexecError);

}  // namespace traceexec
