#pragma once

#include <string>
#include <unistd.h>

namespace shk {

/**
 * This is a helper class that exposes functionality to spawn a shk-trace server
 * process, wait for it to start serving and then sending a SIGTERM to it when
 * destroying the object.
 *
 * The public base class TraceServerHandle is a no-op class that is useful for
 * mocking. To get an instance that actually does something, use
 * TraceServerHandle::open.
 */
class TraceServerHandle {
 public:
  TraceServerHandle() = default;
  TraceServerHandle(const TraceServerHandle &) = delete;
  TraceServerHandle &operator=(const TraceServerHandle &) = delete;
  virtual ~TraceServerHandle() = default;

  virtual const std::string &getShkTracePath() const = 0;

  /**
   * Returns a nullptr unique_ptr on failure.
   *
   * shk_trace_command is the shk-trace command that should be run. It is
   * recommended to let it be of the format "shk-trace -s -O" to enable server
   * mode and to enable suicide-when-orphaned behavior, to avoid having the
   * server process laying around for longer than this process.
   */
  static std::unique_ptr<TraceServerHandle> open(
      const std::string &shk_trace_command,
      std::string *err);
};

}  // namespace shk
