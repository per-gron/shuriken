#include "kdebug_pump.h"

#include <cstdlib>

#include "apsl_code.h"

namespace shk {

static constexpr int EVENT_BASE = 60000;

KdebugPump::KdebugPump(
    int num_cpus,
    KdebugController *kdebug_ctrl,
    Callback callback)
    : _shutting_down(false),
      _shutdown_semaphore(dispatch_semaphore_create(0)),
      _event_buffer(EVENT_BASE * num_cpus),
      _kdebug_ctrl(*kdebug_ctrl),
      _callback(callback) {}

KdebugPump::~KdebugPump() {
  _shutting_down = true;
  if (!wait(DISPATCH_TIME_FOREVER)) {
    fprintf(stderr, "Failed to wait for tracing to finish\n");
    abort();
  }
}

void KdebugPump::start(dispatch_queue_t queue) {
  _kdebug_ctrl.start(_event_buffer.size());

  dispatch_async(queue, ^{ loop(queue); });
}

bool KdebugPump::wait(dispatch_time_t timeout) {
  return dispatch_semaphore_wait(_shutdown_semaphore.get(), timeout) == 0;
}

void KdebugPump::loop(dispatch_queue_t queue) {
  if (_shutting_down) {
    // Signal the semaphore twice, because both the destructor and wait may be
    // waiting for it.
    dispatch_semaphore_signal(_shutdown_semaphore.get());
    dispatch_semaphore_signal(_shutdown_semaphore.get());
    return;
  }

  auto sleep_ms = fetchBuffer(_event_buffer);
  dispatch_time_t time = dispatch_time(
      DISPATCH_TIME_NOW, sleep_ms * 1000 * 1000);
  dispatch_after(time, queue, ^{ loop(queue); });
}

uint64_t KdebugPump::fetchBuffer(std::vector<kd_buf> &event_buffer) {
  size_t count = _kdebug_ctrl.readBuf(event_buffer.data());

  uint64_t sleep_ms = calculateKdebugLoopSleepTime(count, event_buffer.size());

  kd_buf *kd = event_buffer.data();
  if (_callback(kd, kd + count)) {
    _shutting_down = true;
    return 0;
  }

  return sleep_ms;
}

}  // namespace shk
