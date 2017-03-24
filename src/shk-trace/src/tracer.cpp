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

#include "tracer.h"

#include <array>
#include <string>

#include <dispatch/dispatch.h>
#include <sys/mman.h>

namespace shk {
namespace {

using SyscallAtMember = int event_info::*;

SyscallAtMember syscallAtMember(int syscall) {
  switch (syscall) {
  case BSC_openat:
  case BSC_openat_nocancel:
  case BSC_chmodat:
  case BSC_chownat:
  case BSC_fstatat:
  case BSC_linkat:
  case BSC_unlinkat:
  case BSC_readlinkat:
  case BSC_mkdirat:
  case BSC_getattrlistat:
    return &event_info::arg1;
    break;
  case BSC_symlinkat:
    return &event_info::arg2;
    break;
  case BSC_renameat:
    abort();  // Not supported by this function (the answer is two members)
    break;
  default:
    return nullptr;
  }
}

}  // anonymous namespace

static constexpr int PATHLENGTH = NUMPARMS * sizeof(uintptr_t);

static constexpr uint64_t SLEEP_MIN = 1;
static constexpr uint64_t SLEEP_BEHIND = 2;
static constexpr uint64_t SLEEP_MAX = 32;

static constexpr int EVENT_BASE = 60000;
static constexpr int DBG_FUNC_MASK = 0xfffffffc;

Tracer::Tracer(
    int num_cpus,
    KdebugController &kdebug_ctrl,
    Delegate &delegate)
    : _shutting_down(false),
      _shutdown_semaphore(dispatch_semaphore_create(0)),
      _event_buffer(EVENT_BASE * num_cpus),
      _kdebug_ctrl(kdebug_ctrl),
      _delegate(delegate) {}

Tracer::~Tracer() {
  _shutting_down = true;
  if (dispatch_semaphore_wait(
          _shutdown_semaphore.get(), DISPATCH_TIME_FOREVER)) {
    fprintf(stderr, "Failed to wait for tracing to finish\n");
    abort();
  }
}

void Tracer::start(dispatch_queue_t queue) {
  set_remove();
  _kdebug_ctrl.setNumbufs(_event_buffer.size());
  _kdebug_ctrl.setup();

  _kdebug_ctrl.setFilter();
  set_enable(true);

  dispatch_async(queue, ^{ loop(queue); });
}

void Tracer::loop(dispatch_queue_t queue) {
  if (_shutting_down) {
    dispatch_semaphore_signal(_shutdown_semaphore.get());
    return;
  }

  auto sleep_ms = sample_sc(_event_buffer);
  dispatch_time_t time = dispatch_time(DISPATCH_TIME_NOW, sleep_ms * 1000);
  dispatch_after(time, queue, ^{ loop(queue); });
}

void Tracer::set_enable(bool enabled) {
  _kdebug_ctrl.enable(enabled);
  _trace_enabled = enabled;
}

void Tracer::set_remove()  {
  try {
    _kdebug_ctrl.teardown();
  } catch (std::runtime_error &error) {
    if (_trace_enabled) {
      set_enable(false);
    }

    exit(1);
  }
}

uint64_t Tracer::sample_sc(std::vector<kd_buf> &event_buffer) {
  kbufinfo_t bufinfo = _kdebug_ctrl.getBufinfo();

  size_t count = _kdebug_ctrl.readBuf(event_buffer.data(), bufinfo.nkdbufs);

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

  if (bufinfo.flags & KDBG_WRAPPED) {
    throw std::runtime_error("Buffer overrun! Event data has been lost");
  }
  kd_buf *kd = event_buffer.data();

  for (int i = 0; i < count; i++) {
    uintptr_t thread = kd[i].arg5;
    uint32_t debugid = kd[i].debugid;
    int type = kd[i].debugid & DBG_FUNC_MASK;

    switch (type) {
    case TRACE_DATA_NEWTHREAD:
      if (kd[i].arg1) {
        event_info *ei = &_ei_map.add_event(thread, TRACE_DATA_NEWTHREAD)->second;
        ei->child_thread = kd[i].arg1;
        ei->pid = kd[i].arg2;
        _delegate.newThread(ei->pid, thread, ei->child_thread);
      }
      continue;

    case TRACE_STRING_NEWTHREAD:
      {
        auto ei_it = _ei_map.find(thread, TRACE_DATA_NEWTHREAD);
        if (ei_it == _ei_map.end()) {
          continue;
        }
        event_info *ei = &ei_it->second;

        _ei_map.erase(ei_it);
        continue;
      }

    case TRACE_DATA_EXEC:
      {
        auto &ei = _ei_map.add_event(thread, TRACE_DATA_EXEC)->second;
        ei.pid = kd[i].arg1;
        continue;
      }

    case TRACE_STRING_EXEC:
      {
        // TODO(peck): It seems like this code never finds anything in _ei_map
        // in practice. It works because execve and posix_spawn is always
        // accompanied by stat64 calls, which count as reads as well.
        auto ei_it = _ei_map.find(thread, BSC_execve);
        if (ei_it != _ei_map.end()) {
          if (ei_it->second.lookups[0].pathname[0]) {
            exit_event(thread, BSC_execve, 0, 0, 0, 0, BSC_execve);
          }
        } else if ((ei_it = _ei_map.find(thread, BSC_posix_spawn)) != _ei_map.end()) {
          if (ei_it->second.lookups[0].pathname[0]) {
            exit_event(thread, BSC_posix_spawn, 0, 0, 0, 0, BSC_execve);
          }
        }
        ei_it = _ei_map.find(thread, TRACE_DATA_EXEC);

        _ei_map.erase(ei_it);
        continue;
      }

    case BSC_thread_terminate:
      _delegate.terminateThread(thread);
      continue;

    case BSC_exit:
      continue;

    case proc_exit:
      kd[i].arg1 = kd[i].arg2 >> 8;
      type = BSC_exit;
      break;

    case BSC_mmap:
      if (kd[i].arg4 & MAP_ANON) {
        continue;
      }
      break;

    case VFS_ALIAS_VP:
      {
        auto name_it = _vn_name_map.find(kd[i].arg1);
        if (name_it != _vn_name_map.end()) {
          _vn_name_map[kd[i].arg2] = name_it->second;
        } else {
          // TODO(peck): Can this happen?
          _vn_name_map.erase(kd[i].arg2);
        }
        continue;
      }

    case VFS_LOOKUP:
      {
        auto ei_it = _ei_map.find_last(thread);
        if (ei_it == _ei_map.end()) {
          continue;
        }
        event_info *ei = &ei_it->second;

        uintptr_t *sargptr;
        if (debugid & DBG_FUNC_START) {
          if (ei->pn_scall_index < MAX_SCALL_PATHNAMES) {
            ei->pn_work_index = ei->pn_scall_index;
          } else {
            continue;
          }
          sargptr = &ei->lookups[ei->pn_work_index].pathname[0];

          ei->vnodeid = kd[i].arg1;

          *sargptr++ = kd[i].arg2;
          *sargptr++ = kd[i].arg3;
          *sargptr++ = kd[i].arg4;
          *sargptr = 0;

          ei->pathptr = sargptr;
        } else {
          sargptr = ei->pathptr;

          // We don't want to overrun our pathname buffer if the
          // kernel sends us more VFS_LOOKUP entries than we can
          // handle and we only handle 2 pathname lookups for
          // a given system call.
          if (sargptr == 0) {
            continue;
          }

          if ((uintptr_t)sargptr < (uintptr_t)&ei->lookups[ei->pn_work_index].pathname[NUMPARMS]) {
            *sargptr++ = kd[i].arg1;
            *sargptr++ = kd[i].arg2;
            *sargptr++ = kd[i].arg3;
            *sargptr++ = kd[i].arg4;
            *sargptr = 0;
          }
        }
        if (debugid & DBG_FUNC_END) {
          _vn_name_map[ei->vnodeid] =
              reinterpret_cast<const char *>(&ei->lookups[ei->pn_work_index].pathname[0]);

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

        continue;
      }
    }

    if (debugid & DBG_FUNC_START) {
      if ((type & CLASS_MASK) == FILEMGR_BASE) {
        _delegate.fileEvent(
            thread,
            EventType::FATAL_ERROR,
            0,
            "Legacy Carbon FileManager event");
      } else {
        enter_event(thread, type, &kd[i], nullptr);
      }
      continue;
    }

    switch (type) {
    case MACH_pageout:
    case MACH_vmfault:
      {
        auto ei_it = _ei_map.find(thread, type);
        if (ei_it != _ei_map.end()) {
          _ei_map.erase(ei_it);
        }
        continue;
      }
    }

    if (should_process_syscall(type)) {
      exit_event(thread, type, kd[i].arg1, kd[i].arg2, kd[i].arg3, kd[i].arg4, type);
    }
  }
  fflush(0);

  return sleep_ms;
}


void Tracer::enter_event_now(uintptr_t thread, int type, kd_buf *kd, const char *name) {
  auto ei_it = _ei_map.add_event(thread, type);
  auto *ei = &ei_it->second;

  ei->arg1 = kd->arg1;
  ei->arg2 = kd->arg2;
  ei->arg3 = kd->arg3;
  ei->arg4 = kd->arg4;
}


void Tracer::enter_event(uintptr_t thread, int type, kd_buf *kd, const char *name) {
  if (should_process_syscall(type)) {
    enter_event_now(thread, type, kd, name);
  }
}

void Tracer::exit_event(
    uintptr_t thread,
    int type,
    uintptr_t arg1,
    uintptr_t arg2,
    uintptr_t arg3,
    uintptr_t arg4,
    int syscall) {
  auto ei_it = _ei_map.find(thread, type);
  if (ei_it != _ei_map.end()) {
    auto *ei = &ei_it->second;
    format_print(
        ei,
        thread,
        type,
        arg1,
        arg2,
        arg3,
        arg4,
        syscall,
        (char *)&ei->lookups[0].pathname[0],
        (char *)&ei->lookups[1].pathname[0]);
    _ei_map.erase(ei_it);
  }
}


void Tracer::format_print(
    event_info *ei,
    uintptr_t thread,
    int type,
    uintptr_t arg1,
    uintptr_t arg2,
    uintptr_t arg3,
    uintptr_t arg4,
    int syscall,
    const char *pathname1 /* nullable */,
    const char *pathname2 /* nullable */) {
  char buf[(PATHLENGTH + 80) + 64];

  std::array<std::tuple<EventType, const char *, SyscallAtMember>, 4> events{};
  int num_events = 0;
  auto add_event = [&](
      EventType type, const char *pathname, SyscallAtMember at) {
    events[num_events++] = { type, pathname, at };
  };

  const bool success = arg1 == 0;

  switch (syscall) {
  case BSC_dup:
  case BSC_dup2:
  {
    if (success) {
      _delegate.dup(thread, ei->arg1, arg2, /*cloexec:*/false);
    }
    break;
  }

  case BSC_open:
  case BSC_open_nocancel:
  case BSC_open_extended:
  case BSC_guarded_open_np:
  case BSC_open_dprotected_np:
  case BSC_openat:
  case BSC_openat_nocancel:
  {
    bool read = !(ei->arg2 & O_WRONLY);
    bool write = !!(ei->arg2 & O_RDWR) || !!(ei->arg2 & O_WRONLY);
    bool excl = !!(ei->arg2 & O_EXCL);
    bool trunc = !!(ei->arg2 & O_TRUNC);
    auto at = syscallAtMember(syscall);

    if (excl || (read && !trunc)) {
      add_event(EventType::READ, pathname1, at);
    }

    if (trunc) {
      add_event(EventType::CREATE, pathname1, at);
    } else if (write) {
      add_event(EventType::WRITE, pathname1, at);
    }

    if (success) {
      auto fd = arg2;
      _delegate.open(
          thread,
          fd,
          /*at_fd:*/AT_FDCWD,  // TODO(peck): Implement me
          pathname1,
          /*cloexec:*/false);  // TODO(peck): Implement me
    }

    break;
  }

  case BSC_unlink:
  case BSC_unlinkat:
  {
    add_event(EventType::DELETE, pathname1, syscallAtMember(syscall));
    break;
  }

  case BSC_link:
  {
    add_event(EventType::READ, pathname1, nullptr);
    add_event(EventType::CREATE, pathname2, nullptr);
    break;
  }

  case BSC_linkat:
  {
    add_event(EventType::READ, pathname1, &event_info::arg1);
    add_event(EventType::CREATE, pathname2, &event_info::arg3);
    break;
  }

  case BSC_rename:
  {
    add_event(EventType::DELETE, pathname1, nullptr);
    add_event(EventType::CREATE, pathname2, nullptr);
    break;
  }

  case BSC_renameat:
  {
    auto at = syscallAtMember(syscall);
    add_event(EventType::DELETE, pathname1, &event_info::arg1);
    add_event(EventType::CREATE, pathname2, &event_info::arg3);
    break;
  }

  case BSC_mkdir:
  case BSC_mkdir_extended:
  case BSC_mkdirat:
  case BSC_mkfifo:
  case BSC_mkfifo_extended:
  case BSC_symlink:
  case BSC_symlinkat:
  {
    add_event(EventType::CREATE, pathname1, syscallAtMember(syscall));
    break;
  }

  case BSC_truncate:
  {
    add_event(EventType::WRITE, pathname1, nullptr);
    break;
  }

  case BSC_access:
  case BSC_access_extended:
  case BSC_readlink:
  case BSC_readlinkat:
  case BSC_stat:
  case BSC_stat64:
  case BSC_stat_extended:
  case BSC_stat64_extended:
  case BSC_lstat:
  case BSC_lstat64:
  case BSC_lstat_extended:
  case BSC_lstat64_extended:
  case BSC_fstatat:
  case BSC_fstatat64:
  {
    add_event(EventType::READ, pathname1, syscallAtMember(syscall));
    break;
  }


  case BSC_execve:
  case BSC_posix_spawn:
  {
    // TODO(peck): It seems like this code is dead in practice: This code never
    // gets a pathname of the executed binary. It works because execve and
    // posix_spawn is always accompanied by stat64 calls.
    add_event(EventType::READ, pathname1, syscallAtMember(syscall));
    break;
  }
  }

  if (pathname1) {
    for (int i = 0; i < num_events; i++) {
      EventType event = std::get<0>(events[i]);
      const char *path = std::get<1>(events[i]);
      SyscallAtMember at = std::get<2>(events[i]);

      const bool is_modify =
          event == EventType::WRITE ||
          event == EventType::CREATE ||
          event == EventType::DELETE;
      if (success || !is_modify) {
        _delegate.fileEvent(thread, event, at ? ei->*at : AT_FDCWD, path);
      }
    }
  }
}

}  // namespace shk
