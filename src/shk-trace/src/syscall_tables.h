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

namespace shk {

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
};

enum class SyscallAt {
  NO,
  YES
};

struct bsd_syscall {
  static_assert(static_cast<int>(Fmt::IGNORE) == 0, "bsd_syscall should be all zeros by default");
  Fmt format = Fmt::IGNORE;
  SyscallAt at = SyscallAt::NO;
};

static constexpr int MAX_BSD_SYSCALL = 526;

static std::array<bsd_syscall, MAX_BSD_SYSCALL> make_bsd_syscall_table() {
  static const std::tuple<int, Fmt, SyscallAt> bsd_syscall_table[] = {
    { BSC_stat, Fmt::READ_METADATA, SyscallAt::NO },
    { BSC_stat64, Fmt::READ_METADATA, SyscallAt::NO },
    { BSC_stat_extended, Fmt::READ_METADATA, SyscallAt::NO },
    { BSC_stat64_extended, Fmt::READ_METADATA, SyscallAt::NO },
    { BSC_execve, Fmt::IGNORE, SyscallAt::NO },
    { BSC_posix_spawn, Fmt::IGNORE, SyscallAt::NO },
    { BSC_open, Fmt::OPEN, SyscallAt::NO },
    { BSC_open_nocancel, Fmt::OPEN, SyscallAt::NO },
    { BSC_open_extended, Fmt::OPEN, SyscallAt::NO },
    { BSC_guarded_open_np, Fmt::OPEN, SyscallAt::NO },
    { BSC_open_dprotected_np, Fmt::OPEN, SyscallAt::NO },
    { BSC_fstat, Fmt::FD_READ_METADATA, SyscallAt::NO },
    { BSC_fstat64, Fmt::FD_READ_METADATA, SyscallAt::NO },
    { BSC_fstat_extended, Fmt::FD_READ_METADATA, SyscallAt::NO },
    { BSC_fstat64_extended, Fmt::FD_READ_METADATA, SyscallAt::NO },
    { BSC_lstat, Fmt::READ_METADATA, SyscallAt::NO },
    { BSC_lstat64, Fmt::READ_METADATA, SyscallAt::NO },
    { BSC_lstat_extended, Fmt::READ_METADATA, SyscallAt::NO },
    { BSC_lstat64_extended, Fmt::READ_METADATA, SyscallAt::NO },
    { BSC_link, Fmt::CREATE, SyscallAt::NO },
    { BSC_unlink, Fmt::DELETE, SyscallAt::NO },
    { BSC_mknod, Fmt::CREATE, SyscallAt::NO },
    { BSC_chmod, Fmt::WRITE_METADATA, SyscallAt::NO },
    { BSC_chmod_extended, Fmt::WRITE_METADATA, SyscallAt::NO },
    { BSC_fchmod, Fmt::FD_WRITE_METADATA, SyscallAt::NO },
    { BSC_fchmod_extended, Fmt::FD_WRITE_METADATA, SyscallAt::NO },
    { BSC_chown, Fmt::WRITE_METADATA, SyscallAt::NO },
    { BSC_lchown, Fmt::WRITE_METADATA, SyscallAt::NO },
    { BSC_fchown, Fmt::FD_WRITE_METADATA, SyscallAt::NO },
    { BSC_access, Fmt::READ_METADATA, SyscallAt::NO },
    { BSC_access_extended, Fmt::READ_METADATA, SyscallAt::NO },
    { BSC_chdir, Fmt::IGNORE, SyscallAt::NO },
    { BSC_pthread_chdir, Fmt::IGNORE, SyscallAt::NO },
    { BSC_chroot, Fmt::IGNORE, SyscallAt::NO },
    { BSC_utimes, Fmt::WRITE_METADATA, SyscallAt::NO },
    { BSC_delete, Fmt::DELETE, SyscallAt::NO },  // Carbon FileManager related syscall
    { BSC_undelete, Fmt::ILLEGAL, SyscallAt::NO },
    { BSC_chflags, Fmt::WRITE_METADATA, SyscallAt::NO },
    { BSC_fchflags, Fmt::FD_WRITE_METADATA, SyscallAt::NO },
    { BSC_fchdir, Fmt::IGNORE, SyscallAt::NO },
    { BSC_pthread_fchdir, Fmt::IGNORE, SyscallAt::NO },
    { BSC_futimes, Fmt::FD_WRITE_METADATA, SyscallAt::NO },
    { BSC_symlink, Fmt::CREATE, SyscallAt::NO },
    { BSC_readlink, Fmt::READ_CONTENTS, SyscallAt::NO },
    { BSC_mkdir, Fmt::CREATE_DIR, SyscallAt::NO },
    { BSC_mkdir_extended, Fmt::CREATE_DIR, SyscallAt::NO },
    { BSC_mkfifo, Fmt::CREATE, SyscallAt::NO },
    { BSC_mkfifo_extended, Fmt::CREATE, SyscallAt::NO },
    { BSC_rmdir, Fmt::DELETE_DIR, SyscallAt::NO },
    { BSC_getdirentries, Fmt::READ_DIR , SyscallAt::NO },
    { BSC_getdirentries64, Fmt::READ_DIR, SyscallAt::NO },
    { BSC_truncate, Fmt::WRITE_CONTENTS, SyscallAt::NO },
    { BSC_getattrlist, Fmt::READ_METADATA, SyscallAt::NO },
    { BSC_setattrlist, Fmt::WRITE_METADATA, SyscallAt::NO },
    { BSC_fgetattrlist, Fmt::FD_READ_METADATA, SyscallAt::NO },
    { BSC_fsetattrlist, Fmt::FD_WRITE_METADATA, SyscallAt::NO },
    { BSC_getdirentriesattr, Fmt::FD_READ_DIR, SyscallAt::NO },
    { BSC_exchangedata, Fmt::EXCHANGE, SyscallAt::NO },
    { BSC_rename, Fmt::RENAME, SyscallAt::NO },
    { BSC_copyfile, Fmt::CREATE, SyscallAt::NO },
    { BSC_checkuseraccess, Fmt::READ_METADATA, SyscallAt::NO },
    { BSC_searchfs, Fmt::ILLEGAL, SyscallAt::NO },
    { BSC_getattrlistbulk, Fmt::FD_READ_DIR, SyscallAt::NO },
    { BSC_openat, Fmt::OPEN, SyscallAt::YES },
    { BSC_openat_nocancel, Fmt::OPEN, SyscallAt::YES },
    { BSC_renameat, Fmt::RENAME, SyscallAt::YES },
    { BSC_chmodat, Fmt::WRITE_METADATA, SyscallAt::YES },
    { BSC_chownat, Fmt::WRITE_METADATA, SyscallAt::YES },
    { BSC_fstatat, Fmt::FD_READ_METADATA, SyscallAt::YES },
    { BSC_fstatat64, Fmt::FD_READ_METADATA, SyscallAt::NO },
    { BSC_linkat, Fmt::CREATE, SyscallAt::YES },
    { BSC_unlinkat, Fmt::DELETE, SyscallAt::YES },
    { BSC_readlinkat, Fmt::READ_CONTENTS, SyscallAt::YES },
    { BSC_symlinkat, Fmt::CREATE, SyscallAt::YES },
    { BSC_mkdirat, Fmt::CREATE_DIR, SyscallAt::YES },
    { BSC_getattrlistat, Fmt::READ_METADATA, SyscallAt::YES },
  };

  std::array<bsd_syscall, MAX_BSD_SYSCALL> result;
  for (auto syscall_descriptor : bsd_syscall_table) {
    int code = BSC_INDEX(std::get<0>(syscall_descriptor));

    auto &syscall = result.at(code);
    syscall.format = std::get<1>(syscall_descriptor);
    syscall.at = std::get<2>(syscall_descriptor);
  }
  return result;
}

}  // namespace shk
