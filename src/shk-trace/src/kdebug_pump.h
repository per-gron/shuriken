#pragma once

#include <atomic>
#include <functional>
#include <vector>

#include <dispatch/dispatch.h>

#include "dispatch.h"
#include "kdebug.h"
#include "kdebug_controller.h"

namespace shk {

/**
 * KdebugPump is a class that pulls events from KdebugController in a loop,
 * waiting sometimes to poll it at appropriate intervals. The events it gets
 * are emitted to a provided callback.
 */
class KdebugPump {
 public:
  /**
   * Callback that processes Kdebug data. If the callback returns true,
   * KdebugPump will stop its tracing.
   */
  using Callback = std::function<bool (const kd_buf *begin, const kd_buf *end)>;

  KdebugPump(
      int num_cpus,
      KdebugController *kdebug_ctrl,
      Callback callback);
  KdebugPump(const KdebugPump &) = delete;
  KdebugPump &operator=(const KdebugPump &) = delete;
  ~KdebugPump();

  void start(dispatch_queue_t queue);

  /**
   * Block until the tracing server has stopped listening (this happens when the
   * delegate instructs it to quit). This method can be called from any thread.
   *
   * Returns true on success, or false on timeout.
   *
   * This method should be called at most once.
   */
  bool wait(dispatch_time_t timeout);

 private:
  void loop(dispatch_queue_t queue);
  uint64_t fetchBuffer(std::vector<kd_buf> &event_buffer);

  std::atomic<bool> _shutting_down;
  DispatchSemaphore _shutdown_semaphore;
  std::vector<kd_buf> _event_buffer;

  KdebugController &_kdebug_ctrl;
  const Callback _callback;
};

}  // namespace shk
