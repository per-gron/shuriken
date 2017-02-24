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

#pragma once

#include <array>

#include "syscall_constants.h"

enum class Fmt {
  IGNORE,
  ILLEGAL,
  CREATE,
  DELETE,
  READ_CONTENTS,
  WRITE_CONTENTS,
  READ_METADATA,
  WRITE_METADATA,
  FD_READ_METADATA,
  FD_WRITE_METADATA,
  CREATE_DIR,
  DELETE_DIR,
  READ_DIR,
  FD_READ_DIR,
  EXCHANGE,
  RENAME,
  OPEN,
  HFS_update,
};

enum class SyscallAt {
  NO,
  YES
};

struct bsd_syscall {
  static_assert(static_cast<int>(Fmt::IGNORE) == 0, "bsd_syscall should be all zeros by default");
  const char *name = nullptr;
  Fmt format = Fmt::IGNORE;
  SyscallAt at = SyscallAt::NO;
};

static constexpr int MAX_BSD_SYSCALL = 526;

std::array<bsd_syscall, MAX_BSD_SYSCALL> make_bsd_syscall_table() {
  static const std::tuple<int, const char *, Fmt, SyscallAt> bsd_syscall_table[] = {
    { BSC_stat, "stat", Fmt::READ_METADATA, SyscallAt::NO },
    { BSC_stat64, "stat64", Fmt::READ_METADATA, SyscallAt::NO },
    { BSC_stat_extended, "stat_extended", Fmt::READ_METADATA, SyscallAt::NO },
    { BSC_stat64_extended, "stat_extended64", Fmt::READ_METADATA, SyscallAt::NO },
    { BSC_execve, "execve", Fmt::IGNORE, SyscallAt::NO },
    { BSC_posix_spawn, "posix_spawn", Fmt::IGNORE, SyscallAt::NO },
    { BSC_open, "open", Fmt::OPEN, SyscallAt::NO },
    { BSC_open_nocancel, "open", Fmt::OPEN, SyscallAt::NO },
    { BSC_open_extended, "open_extended", Fmt::OPEN, SyscallAt::NO },
    { BSC_guarded_open_np, "guarded_open_np", Fmt::OPEN, SyscallAt::NO },
    { BSC_open_dprotected_np, "open_dprotected", Fmt::OPEN, SyscallAt::NO },
    { BSC_fstat, "fstat", Fmt::FD_READ_METADATA, SyscallAt::NO },
    { BSC_fstat64, "fstat64", Fmt::FD_READ_METADATA, SyscallAt::NO },
    { BSC_fstat_extended, "fstat_extended", Fmt::FD_READ_METADATA, SyscallAt::NO },
    { BSC_fstat64_extended, "fstat64_extended", Fmt::FD_READ_METADATA, SyscallAt::NO },
    { BSC_lstat, "lstat", Fmt::READ_METADATA, SyscallAt::NO },
    { BSC_lstat64, "lstat64", Fmt::READ_METADATA, SyscallAt::NO },
    { BSC_lstat_extended, "lstat_extended", Fmt::READ_METADATA, SyscallAt::NO },
    { BSC_lstat64_extended, "lstat_extended64", Fmt::READ_METADATA, SyscallAt::NO },
    { BSC_link, "link", Fmt::CREATE, SyscallAt::NO },
    { BSC_unlink, "unlink", Fmt::DELETE, SyscallAt::NO },
    { BSC_mknod, "mknod", Fmt::CREATE, SyscallAt::NO },
    { BSC_chmod, "chmod", Fmt::WRITE_METADATA, SyscallAt::NO },
    { BSC_chmod_extended, "chmod_extended", Fmt::WRITE_METADATA, SyscallAt::NO },
    { BSC_fchmod, "fchmod", Fmt::FD_WRITE_METADATA, SyscallAt::NO },
    { BSC_fchmod_extended, "fchmod_extended", Fmt::FD_WRITE_METADATA, SyscallAt::NO },
    { BSC_chown, "chown", Fmt::WRITE_METADATA, SyscallAt::NO },
    { BSC_lchown, "lchown", Fmt::WRITE_METADATA, SyscallAt::NO },
    { BSC_fchown, "fchown", Fmt::FD_WRITE_METADATA, SyscallAt::NO },
    { BSC_access, "access", Fmt::READ_METADATA, SyscallAt::NO },
    { BSC_access_extended, "access_extended", Fmt::READ_METADATA, SyscallAt::NO },
    { BSC_chdir, "chdir", Fmt::IGNORE, SyscallAt::NO },
    { BSC_pthread_chdir, "pthread_chdir", Fmt::IGNORE, SyscallAt::NO },
    { BSC_chroot, "chroot", Fmt::IGNORE, SyscallAt::NO },
    { BSC_utimes, "utimes", Fmt::WRITE_METADATA, SyscallAt::NO },
    { BSC_delete, "delete-Carbon", Fmt::DELETE, SyscallAt::NO },
    { BSC_undelete, "undelete", Fmt::CREATE, SyscallAt::NO },
    { BSC_chflags, "chflags", Fmt::WRITE_METADATA, SyscallAt::NO },
    { BSC_fchflags, "fchflags", Fmt::FD_WRITE_METADATA, SyscallAt::NO },
    { BSC_fchdir, "fchdir", Fmt::IGNORE, SyscallAt::NO },
    { BSC_pthread_fchdir, "pthread_fchdir", Fmt::IGNORE, SyscallAt::NO },
    { BSC_futimes, "futimes", Fmt::FD_WRITE_METADATA, SyscallAt::NO },
    { BSC_symlink, "symlink", Fmt::CREATE, SyscallAt::NO },
    { BSC_readlink, "readlink", Fmt::READ_CONTENTS, SyscallAt::NO },
    { BSC_mkdir, "mkdir", Fmt::CREATE_DIR, SyscallAt::NO },
    { BSC_mkdir_extended, "mkdir_extended", Fmt::CREATE_DIR, SyscallAt::NO },
    { BSC_mkfifo, "mkfifo", Fmt::CREATE, SyscallAt::NO },
    { BSC_mkfifo_extended, "mkfifo_extended", Fmt::CREATE, SyscallAt::NO },
    { BSC_rmdir, "rmdir", Fmt::DELETE_DIR, SyscallAt::NO },
    { BSC_getdirentries, "getdirentries", Fmt::READ_DIR , SyscallAt::NO },
    { BSC_getdirentries64, "getdirentries64", Fmt::READ_DIR, SyscallAt::NO },
    { BSC_truncate, "truncate", Fmt::WRITE_CONTENTS, SyscallAt::NO },
    { BSC_getattrlist, "getattrlist", Fmt::READ_METADATA, SyscallAt::NO },
    { BSC_setattrlist, "setattrlist", Fmt::WRITE_METADATA, SyscallAt::NO },
    { BSC_fgetattrlist, "fgetattrlist", Fmt::FD_READ_METADATA, SyscallAt::NO },
    { BSC_fsetattrlist, "fsetattrlist", Fmt::FD_WRITE_METADATA, SyscallAt::NO },
    { BSC_getdirentriesattr, "getdirentriesattr", Fmt::FD_READ_DIR, SyscallAt::NO },
    { BSC_exchangedata, "exchangedata", Fmt::EXCHANGE, SyscallAt::NO },
    { BSC_rename, "rename", Fmt::RENAME, SyscallAt::NO },
    { BSC_copyfile, "copyfile", Fmt::CREATE, SyscallAt::NO },
    { BSC_checkuseraccess, "checkuseraccess", Fmt::READ_METADATA, SyscallAt::NO },
    { BSC_searchfs, "searchfs", Fmt::ILLEGAL, SyscallAt::NO },
    { BSC_getattrlistbulk, "getattrlistbulk", Fmt::FD_READ_DIR, SyscallAt::NO },
    { BSC_openat, "openat", Fmt::OPEN, SyscallAt::YES },
    { BSC_openat_nocancel, "openat", Fmt::OPEN, SyscallAt::YES },
    { BSC_renameat, "renameat", Fmt::RENAME, SyscallAt::YES },
    { BSC_chmodat, "chmodat", Fmt::WRITE_METADATA, SyscallAt::YES },
    { BSC_chownat, "chownat", Fmt::WRITE_METADATA, SyscallAt::YES },
    { BSC_fstatat, "fstatat", Fmt::FD_READ_METADATA, SyscallAt::YES },
    { BSC_fstatat64, "fstatat64", Fmt::FD_READ_METADATA, SyscallAt::NO },
    { BSC_linkat, "linkat", Fmt::CREATE, SyscallAt::YES },
    { BSC_unlinkat, "unlinkat", Fmt::DELETE, SyscallAt::YES },
    { BSC_readlinkat, "readlinkat", Fmt::READ_CONTENTS, SyscallAt::YES },
    { BSC_symlinkat, "symlinkat", Fmt::CREATE, SyscallAt::YES },
    { BSC_mkdirat, "mkdirat", Fmt::CREATE_DIR, SyscallAt::YES },
    { BSC_getattrlistat, "getattrlistat", Fmt::READ_METADATA, SyscallAt::YES },
  };

  std::array<bsd_syscall, MAX_BSD_SYSCALL> result;
  for (auto syscall_descriptor : bsd_syscall_table) {
    int code = BSC_INDEX(std::get<0>(syscall_descriptor));

    auto &syscall = result.at(code);
    syscall.name = std::get<1>(syscall_descriptor);
    syscall.format = std::get<2>(syscall_descriptor);
    syscall.at = std::get<3>(syscall_descriptor);
  }
  return result;
}
