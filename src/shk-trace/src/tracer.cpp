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

#include "syscall_tables.h"

namespace shk {
namespace {

using SyscallAtMember = int EventInfo::*;

SyscallAtMember syscallAtMember(int syscall) {
  switch (syscall) {
  case BSC_chmodat:
  case BSC_chownat:
  case BSC_faccessat:
  case BSC_fstatat:
  case BSC_fstatat64:
  case BSC_getattrlistat:
  case BSC_linkat:
  case BSC_mkdirat:
  case BSC_openat:
  case BSC_openat_nocancel:
  case BSC_readlinkat:
  case BSC_unlinkat:
    return &EventInfo::arg1;
    break;
  case BSC_symlinkat:
    return &EventInfo::arg2;
    break;
  default:
    return nullptr;
  }
}

}  // anonymous namespace

static constexpr int DBG_FUNC_MASK = 0xfffffffc;

Tracer::Tracer(Delegate &delegate)
    : _delegate(delegate) {}

bool Tracer::parseBuffer(const kd_buf *begin, const kd_buf *end) {
  for (auto it = begin; it != end; ++it) {
    const auto &kd = *it;

    uintptr_t thread = kd.arg5;
    uint32_t debugid = kd.debugid;
    int type = kd.debugid & DBG_FUNC_MASK;

    switch (type) {
    case TRACE_DATA_NEWTHREAD:
      {
        auto child_thread = kd.arg1;
        auto pid = kd.arg2;
        if (child_thread) {
          _delegate.newThread(pid, thread, child_thread);
        }
        continue;
      }

    case TRACE_STRING_EXEC:
      {
        auto ei_it = _ei_map.find(thread, BSC_execve);
        if (ei_it != _ei_map.end()) {
          if (ei_it->second.lookups[0].pathname[0]) {
            exitEvent(thread, BSC_execve, 0, 0, 0, 0, BSC_execve);
          }
        } else if (
            (ei_it = _ei_map.find(thread, BSC_posix_spawn)) != _ei_map.end()) {
          if (ei_it->second.lookups[0].pathname[0]) {
            exitEvent(thread, BSC_posix_spawn, 0, 0, 0, 0, BSC_execve);
          }
        }

        continue;
      }

    case BSC_thread_terminate:
      if (_delegate.terminateThread(thread) ==
          Delegate::Response::QUIT_TRACING) {
        return true;
      }
      continue;

    case VFS_LOOKUP:
      {
        auto ei_it = _ei_map.findLast(thread);
        if (ei_it == _ei_map.end()) {
          continue;
        }
        EventInfo *ei = &ei_it->second;

        uintptr_t *sargptr;
        if (debugid & DBG_FUNC_START) {
          if (ei->pn_scall_index < MAX_SCALL_PATHNAMES) {
            ei->pn_work_index = ei->pn_scall_index;
          } else {
            continue;
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

          // We don't want to overrun our pathname buffer if the
          // kernel sends us more VFS_LOOKUP entries than we can
          // handle and we only handle 2 pathname lookups for
          // a given system call.
          if (sargptr == 0) {
            continue;
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
        if (debugid & DBG_FUNC_END) {
          _vn_name_map[ei->vnodeid] =
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
        enterEvent(thread, type, kd);
      }
      continue;
    }

    if (should_process_syscall(type)) {
      exitEvent(
          thread, type, kd.arg1, kd.arg2, kd.arg3, kd.arg4, type);
    }
  }

  return false;
}

void Tracer::enterEvent(uintptr_t thread, int type, const kd_buf &kd) {
  if (should_process_syscall(type)) {
    auto ei_it = _ei_map.addEvent(thread, type);
    auto *ei = &ei_it->second;

    ei->arg1 = kd.arg1;
    ei->arg2 = kd.arg2;
    ei->arg3 = kd.arg3;
    ei->arg4 = kd.arg4;
  }
}

void Tracer::exitEvent(
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
    notifyDelegate(
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

void Tracer::notifyDelegate(
    EventInfo *ei,
    uintptr_t thread,
    int type,
    uintptr_t arg1,
    uintptr_t arg2,
    uintptr_t arg3,
    uintptr_t arg4,
    int syscall,
    const char *pathname1 /* nullable */,
    const char *pathname2 /* nullable */) {
  std::array<std::tuple<EventType, const char *, SyscallAtMember>, 4> events{};
  int num_events = 0;
  auto add_event = [&](
      EventType type, const char *pathname, SyscallAtMember at) {
    events[num_events++] = { type, pathname, at };
  };

  auto disallowed_event = [&](const std::string &event_name) {
    _delegate.fileEvent(
        thread, EventType::FATAL_ERROR, -1, event_name + " not allowed");
  };

  const bool success = arg1 == 0;

  if (!*pathname1) {
    // When opening root, the VFS_LOOKUP mechanism doesn't seem to work; it
    // it results in an empty path, which by the rest of the code is treated as
    // pointing to the cwd or (in *at syscalls) what the file descriptor points
    // to.
    //
    // This is a workaround for this. Unfortunately, this means that shk-trace
    // will often report that root has been read.
    pathname1 = "/";
  }

  switch (syscall) {
  case BSC_dup:
  case BSC_dup2:
  {
    if (success) {
      _delegate.dup(thread, ei->arg1, arg2, /*cloexec:*/false);
    }
    break;
  }

  case BSC_chdir:
  {
    if (success) {
      _delegate.chdir(thread, pathname1, AT_FDCWD);
    }
    break;
  }

  case BSC_fchdir:
  {
    if (success) {
      _delegate.chdir(thread, "", ei->arg1);
    }
    break;
  }

  case BSC_pthread_chdir:
  {
    if (success) {
      _delegate.threadChdir(thread, pathname1, AT_FDCWD);
    }
    break;
  }

  case BSC_pthread_fchdir:
  {
    if (success) {
      _delegate.threadChdir(thread, "", ei->arg1);
    }
    break;
  }

  case BSC_open:
  case BSC_open_nocancel:
  case BSC_open_extended:
  case BSC_guarded_open_dprotected_np:
  case BSC_guarded_open_np:
  case BSC_open_dprotected_np:
  case BSC_openat:
  case BSC_openat_nocancel:
  {
    auto at = syscallAtMember(syscall);
    int flags = at ? ei->arg3 : ei->arg2;
    bool read = !(flags & O_WRONLY);
    bool write = !!(flags & O_RDWR) || !!(flags & O_WRONLY);
    bool excl = !!(flags & O_EXCL);
    bool trunc = !!(flags & O_TRUNC);
    bool cloexec = !!(flags & O_CLOEXEC);

    // open with O_EXCL counts as acquiring information about a potentially
    // pre-existing file. The only difference it makes is that it lets you know
    // if the file was already there.
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
          at ? ei->*at : AT_FDCWD,
          pathname1,
          /*cloexec:*/cloexec);
    }

    break;
  }

  case BSC_fcntl:
  case BSC_fcntl_nocancel:
  {
    int fd = ei->arg1;
    int cmd = ei->arg2;
    int arg = ei->arg3;

    if (cmd == F_DUPFD || cmd == F_DUPFD_CLOEXEC) {
      if (success) {
        _delegate.dup(thread, fd, arg2, cmd == F_DUPFD_CLOEXEC);
      }
    } else if (cmd == F_SETFD) {
      _delegate.setCloexec(thread, fd, arg == FD_CLOEXEC);
    }

    break;
  }

  case BSC_close:
  case BSC_close_nocancel:
  case BSC_guarded_close_np:
  {
    if (success) {
      // In practice, this call does not seem entirely necessary for
      // correctness: If an *at syscall later uses this closed fd, it's going
      // to fail before it even attempts to look up any path, so it's not going
      // to be reported anyway.
      //
      // Nevertheless, this is useful from a resource saving perspective: We
      // don't need to store information about this file descriptor anymore.
      _delegate.close(thread, ei->arg1);
    }
    break;
  }

  case BSC_rmdir:
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
    add_event(EventType::READ, pathname1, &EventInfo::arg1);
    add_event(EventType::CREATE, pathname2, &EventInfo::arg3);
    break;
  }

  case BSC_exchangedata:
  {
    add_event(EventType::WRITE, pathname1, nullptr);
    add_event(EventType::WRITE, pathname2, nullptr);
    break;
  }

  case BSC_rename:
  {
    add_event(EventType::DELETE, pathname1, nullptr);
    add_event(EventType::CREATE, pathname2, nullptr);
    break;
  }

  case BSC_renameat:
  case BSC_renameatx_np:
  {
    add_event(EventType::DELETE, pathname1, &EventInfo::arg1);
    add_event(EventType::CREATE, pathname2, &EventInfo::arg3);
    break;
  }

  case BSC_getattrlistbulk:
  case BSC_getdirentries:
  case BSC_getdirentries64:
  case BSC_getdirentriesattr:
  {
    add_event(EventType::READ_DIRECTORY, "", &EventInfo::arg1);
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

  case BSC_chflags:
  case BSC_chmod:
  case BSC_chmod_extended:
  case BSC_chown:
  case BSC_chmodat:  // This constant actually refers to the fchmodat syscall
  case BSC_chownat:  // This constant actually refers to the fchownat syscall
  case BSC_truncate:
  case BSC_lchown:
  case BSC_removexattr:
  case BSC_setattrlist:
  case BSC_setxattr:
  case BSC_utimes:
  {
    add_event(EventType::WRITE, pathname1, syscallAtMember(syscall));
    break;
  }

  case BSC_fchflags:
  case BSC_fchmod:
  case BSC_fchmod_extended:
  case BSC_fchown:
  case BSC_flock:
  case BSC_fremovexattr:
  case BSC_fsetattrlist:
  case BSC_fsetxattr:
  case BSC_futimes:
  {
    add_event(EventType::WRITE, "", &EventInfo::arg1);
    break;
  }

  case BSC_execve:
  {
    add_event(EventType::READ, pathname1, syscallAtMember(syscall));
    if (success) {
      _delegate.exec(thread);
    }
    break;
  }

  case BSC_access:
  case BSC_faccessat:
  case BSC_fstatat64:
  case BSC_fstatat:
  case BSC_getattrlist:
  case BSC_getattrlistat:
  case BSC_getxattr:
  case BSC_listxattr:
  case BSC_lstat64:
  case BSC_lstat64_extended:
  case BSC_lstat:
  case BSC_lstat_extended:
  case BSC_pathconf:
  case BSC_posix_spawn:
  case BSC_readlink:
  case BSC_readlinkat:
  case BSC_stat64:
  case BSC_stat64_extended:
  case BSC_stat:
  case BSC_stat_extended:
  {
    add_event(EventType::READ, pathname1, syscallAtMember(syscall));
    break;
  }

  case BSC_access_extended:
  {
    // This syscall can ask for info about an unbounded number of paths. I think
    // it might be possible for this code to support that but right now it
    // doesn't and given how undocumented + rare this syscall seems to be I
    // don't want to implement it right now.
    disallowed_event("accessx_np");
    break;
  }

  case BSC_copyfile:
  {
    // This syscall is not supported on HFS+, so 1) it doesn't seem important
    // to do, 2) there is no easy good way to test it.
    disallowed_event("copyfile");
    break;
  }

  case BSC_delete:
  {
    disallowed_event("delete");
    break;
  }

  case BSC_chroot:
  {
    disallowed_event("chroot");
    break;
  }

  case BSC_searchfs:
  {
    disallowed_event("searchfs");
    break;
  }

  case BSC_undelete:
  {
    disallowed_event("undelete");
    break;
  }

  case BSC_mknod:
  {
    disallowed_event("mknod");
    break;
  }

  case BSC_fhopen:
  {
    disallowed_event("fhopen");
    break;
  }

  case BSC_fsgetpath:
  {
    disallowed_event("fsgetpath");
    break;
  }

  case BSC_openbyid_np:
  {
    disallowed_event("openbyid_np");
    break;
  }
  }

  for (int i = 0; i < num_events; i++) {
    EventType event = std::get<0>(events[i]);
    const char *path_c_str = std::get<1>(events[i]);
    SyscallAtMember at = std::get<2>(events[i]);

    if (path_c_str) {
      std::string path = path_c_str;
      // For some weird reason, if a to access a file within a directory that
      // does not exist is made, for example to /nonexisting/file, kdebug will
      // report the path as something along the lines of "/nonexisting>>>>>>>>".
      // We only care about the path prior to the made up > characters, so they
      // are removed.
      //
      // Unfortunately, this means that shk-trace can not correctly trace file
      // accesses to paths that end with a > character.
      auto gt_pos = path.find_last_not_of('>');
      if (gt_pos != std::string::npos) {
        path.resize(gt_pos + 1);
      }

      const bool is_modify =
          event == EventType::WRITE ||
          event == EventType::CREATE ||
          event == EventType::DELETE ||
          event == EventType::READ_DIRECTORY;

      _delegate.fileEvent(
          thread,
          // Modify events, when they fail, potentially expose information about
          // a file or directory at that path, even if they don't modify the
          // file system.
          !success && is_modify ? EventType::READ : event,
          at ? ei->*at : AT_FDCWD,
          std::move(path));
    }
  }
}

}  // namespace shk
