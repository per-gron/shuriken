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

#include <bitset>

#include "syscall_constants.h"

namespace shk {

static constexpr int MAX_BSD_SYSCALL = 526;

static std::bitset<MAX_BSD_SYSCALL> make_bsd_syscall_mask() {
  static const int bsd_syscalls[] = {
    BSC_chdir,
    BSC_fchdir,
    BSC_pthread_chdir,
    BSC_pthread_fchdir,
    BSC_dup,
    BSC_dup2,
    BSC_execve,
    BSC_posix_spawn,
    BSC_stat,
    BSC_stat64,
    BSC_stat_extended,
    BSC_stat64_extended,
    BSC_open,
    BSC_open_nocancel,
    BSC_open_extended,
    BSC_guarded_open_np,
    BSC_open_dprotected_np,
    BSC_fstat,
    BSC_fstat64,
    BSC_fstat_extended,
    BSC_fstat64_extended,
    BSC_lstat,
    BSC_lstat64,
    BSC_lstat_extended,
    BSC_lstat64_extended,
    BSC_link,
    BSC_unlink,
    BSC_mknod,
    BSC_chmod,
    BSC_chmod_extended,
    BSC_fchmod,
    BSC_fchmod_extended,
    BSC_chown,
    BSC_lchown,
    BSC_fchown,
    BSC_access,
    BSC_access_extended,
    BSC_utimes,
    BSC_delete,
    BSC_undelete,
    BSC_chflags,
    BSC_fchflags,
    BSC_futimes,
    BSC_symlink,
    BSC_readlink,
    BSC_mkdir,
    BSC_mkdir_extended,
    BSC_mkfifo,
    BSC_mkfifo_extended,
    BSC_rmdir,
    BSC_getdirentries,
    BSC_getdirentries64,
    BSC_truncate,
    BSC_getattrlist,
    BSC_setattrlist,
    BSC_fgetattrlist,
    BSC_fsetattrlist,
    BSC_getdirentriesattr,
    BSC_exchangedata,
    BSC_rename,
    BSC_copyfile,
    BSC_checkuseraccess,
    BSC_searchfs,
    BSC_getattrlistbulk,
    BSC_openat,
    BSC_openat_nocancel,
    BSC_renameat,
    BSC_chmodat,
    BSC_chownat,
    BSC_fstatat,
    BSC_fstatat64,
    BSC_linkat,
    BSC_unlinkat,
    BSC_readlinkat,
    BSC_symlinkat,
    BSC_mkdirat,
    BSC_getattrlistat,
  };

  std::bitset<MAX_BSD_SYSCALL> result;
  for (auto syscall : bsd_syscalls) {
    result.set(BSC_INDEX(syscall));
  }
  return result;
}

static bool should_process_syscall(int syscall) {
  static const auto bsd_syscall_mask = make_bsd_syscall_mask();

  if ((syscall & CSC_MASK) == BSC_BASE) {
    int index = BSC_INDEX(syscall);
    return index < bsd_syscall_mask.size() ?
        bsd_syscall_mask[BSC_INDEX(syscall)] :
        false;
  } else {
    return false;
  }
}

}  // namespace shk
