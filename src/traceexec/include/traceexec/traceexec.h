#pragma once

#include <functional>

#include <traceexec/traceexec_error.h>

#include <util/raii_helper.h>

namespace traceexec {

using Socket = util::RAIIHelper<int, int, close, -1>;

/**
 * Open a socket to the traceexec kernel extension and start tracing the current
 * process.
 *
 * Throws TraceexecError if the kernel extension is not loaded, if the kernel
 * extension's version is not compatible with this library or if the operation
 * fails for some other reason.
 */
Socket openSocket() throw(TraceexecError);

enum class EventType {
  FILE_READ_METADATA,
  FILE_READ_DATA,
  FILE_WRITE_CREATE,
  FILE_WRITE_DATA,
  FILE_WRITE_FLAGS,
  FILE_WRITE_MODE,
  FILE_WRITE_OWNER,
  FILE_WRITE_SETUGID,
  FILE_REVOKE,
  FILE_WRITE_UNLINK,
  FILE_IOCTL,

  FILE_IOCTL_WRITE_XATTR,
  FILE_READ_XATTR,

  FILE_WRITE_UNMOUNT,
  FILE_WRITE_MOUNT,
  FILE_WRITE_TIMES,

  PROCESS_STAR,
  PROCESS_EXEC,
  PROCESS_EXEC_STAR,
  PROCESS_FORK,

  SIGNAL,

  NETWORK_STAR,
  NETWORK_INBOUND,
  NETWORK_OUTBOUND,
  NETWORK_BIND,

  SYSCTL_STAR,
  SYSCTL_READ,
  SYSCTL_WRITE,

  SYSTEM_STAR,
  SYSTEM_ACCT,
  SYSTEM_AUDIT,
  SYSTEM_FSCTL,
  SYSTEM_LCID,
  SYSTEM_MAC_LABEL,
  SYSTEM_NFSSVC,
  SYSTEM_REBOOT,
  SYSTEM_SET_TIME,
  SYSTEM_SOCKET,
  SYSTEM_SWAP,
  SYSTEM_WRITE_BOOTSTRAP,

  JOB_CREATION,

  IPC_STAR,
  IPC_POSIX_STAR,
  IPC_POSIX_SEM,
  IPC_POSIX_SHM,
  IPC_SYSV_STAR,
  IPC_SYSV_MSG,
  IPC_SYSV_SEM,
  IPC_SYSV_SHM,

  MACH_STAR,
  MACH_PER_USER_LOOKUP,
  MACH_BOOTSTRAP,
  MACH_LOOKUP,
  MACH_PRIV_STAR,
  MACH_PRIV_HOST_PORT,
  MACH_PRIV_TASK_PORT,
  MACH_TASK_NAME,
};

struct Event {
  EventType type;
  std::string target;
};

class Parser {
 public:
  virtual ~Parser() = default;

  virtual bool read(void *buf, size_t len) = 0;
};

std::unique_ptr<Parser> makeParser(
    const std::function<void (Event &&event)> &on_event);

/**
 * Ask the kernel to stop tracing a process and to send an event that indicates
 * that tracing has stopped. This is a useful operation to do for example if the
 * traced process has exited and the code that is interested in tracing it wants
 * to make sure to receive all events. It avoid the race that would occur if the
 * user of this API would just close the tracing socket as soon as the traced
 * process exits: There could be pending events that would then be lost.
 *
 * stopTracing takes a file descriptor as a parameter as returned by openSocket.
 * It is an error to use it with any other file descriptor.
 *
 * If stopTracing is called multiple times for a single socket, the calls that
 * follow the first one has no effect.
 *
 * It is not necessary to call stopTracing from a resource leak perspective:
 * Closing the file descriptor is all that is needed to reclaim resources.
 */
void stopTracing(int fd) throw(TraceexecError);

}  // namespace traceexec
