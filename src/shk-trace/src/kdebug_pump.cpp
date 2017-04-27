/*
 * Copyright (c) 1999 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * "Portions Copyright (c) 1999 Apple Computer, Inc.  All Rights
 * Reserved.  This file contains Original Code and/or Modifications of
 * Original Code as defined in and that are subject to the Apple Public
 * Source License Version 1.0 (the 'License').  You may not use this file
 * except in compliance with the License.  Please obtain a copy of the
 * License at http://www.apple.com/publicsource and read it before using
 * this file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License."
 *
 * @APPLE_LICENSE_HEADER_END@
 */

#include "kdebug_pump.h"

#include <cstdlib>

namespace shk {

static constexpr uint64_t SLEEP_MIN = 1;
static constexpr uint64_t SLEEP_BEHIND = 2;
static constexpr uint64_t SLEEP_MAX = 32;

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

  uint64_t sleep_ms = SLEEP_MIN;
  if (count > (event_buffer.size() / 8)) {
    if (sleep_ms > SLEEP_BEHIND) {
      sleep_ms = SLEEP_BEHIND;
    } else if (sleep_ms > SLEEP_MIN) {
      sleep_ms /= 2;
    }
  } else if (count < (event_buffer.size() / 16)) {
    if (sleep_ms < SLEEP_MAX) {
      sleep_ms *= 2;
    }
  }

  kd_buf *kd = event_buffer.data();
  if (_callback(kd, kd + count)) {
    _shutting_down = true;
    return 0;
  }

  return sleep_ms;
}

}  // namespace shk
