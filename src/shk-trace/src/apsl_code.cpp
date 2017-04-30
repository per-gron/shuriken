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

#include "apsl_code.h"

namespace shk {

uint64_t calculateKdebugLoopSleepTime(size_t count, size_t event_buffer_size) {
  static constexpr uint64_t SLEEP_MIN = 1;
  static constexpr uint64_t SLEEP_BEHIND = 2;
  static constexpr uint64_t SLEEP_MAX = 32;

  uint64_t sleep_ms = SLEEP_MIN;
  if (count > (event_buffer_size / 8)) {
    if (sleep_ms > SLEEP_BEHIND) {
      sleep_ms = SLEEP_BEHIND;
    } else if (sleep_ms > SLEEP_MIN) {
      sleep_ms /= 2;
    }
  } else if (count < (event_buffer_size / 16)) {
    if (sleep_ms < SLEEP_MAX) {
      sleep_ms *= 2;
    }
  }

  return sleep_ms;
}

void processVfsLookup(
    const kd_buf &kd,
    EventInfo *ei /* nullable */,
    std::unordered_map<uint64_t, std::string> *vn_name_map) {
  if (!ei || ei->in_hfs_update) {
    // If no event was found there is nothing to do. If we are in an HFS_update,
    // ignore the path. HFS_update events can happen in the middle of other
    // syscalls, and HFS_update events can emit paths that are unrelated to the
    // syscall that is in progress of being processed, which messes things up
    // unless they are explicitly ignored.
    return;
  }

  uintptr_t *sargptr;
  if (kd.debugid & DBG_FUNC_START) {
    if (ei->pn_scall_index < MAX_SCALL_PATHNAMES) {
      ei->pn_work_index = ei->pn_scall_index;
    } else {
      return;
    }
    sargptr = &ei->lookups[ei->pn_work_index].pathname[0];

    ei->vnodeid = kd.arg1;

    *sargptr++ = kd.arg2;
    *sargptr++ = kd.arg3;
    *sargptr++ = kd.arg4;
    *sargptr = 0;

    ei->pathptr = sargptr;
  } else {
    sargptr = ei->pathptr;

    // We don't want to overrun our pathname buffer if the kernel sends us more
    // VFS_LOOKUP entries than we can handle and we only handle 2 pathname
    // lookups for a given system call.
    if (sargptr == 0) {
      return;
    }

    if ((uintptr_t)sargptr <
        (uintptr_t)&ei->lookups[ei->pn_work_index].pathname[NUMPARMS]) {
      *sargptr++ = kd.arg1;
      *sargptr++ = kd.arg2;
      *sargptr++ = kd.arg3;
      *sargptr++ = kd.arg4;
      *sargptr = 0;
    }
  }
  if (kd.debugid & DBG_FUNC_END) {
    (*vn_name_map)[ei->vnodeid] =
        reinterpret_cast<const char *>(
            &ei->lookups[ei->pn_work_index].pathname[0]);

    if (ei->pn_work_index == ei->pn_scall_index) {
      ei->pn_scall_index++;

      if (ei->pn_scall_index < MAX_SCALL_PATHNAMES) {
        ei->pathptr = &ei->lookups[ei->pn_scall_index].pathname[0];
      } else {
        ei->pathptr = 0;
      }
    }
  } else {
    ei->pathptr = sargptr;
  }
}

}  // namespace shk
