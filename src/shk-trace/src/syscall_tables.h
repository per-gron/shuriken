#pragma once

#include <bitset>

#include "syscall_constants.h"

namespace shk {

static constexpr int MAX_BSD_SYSCALL = 526;

// These constants are defined here and not in syscall_constants.h because that
// file is derived from trace.codes in the kernel, but that file does not have
// this constant defined in it.
static constexpr int BSC_faccessat = 0x040c0748;

static std::bitset<MAX_BSD_SYSCALL> make_bsd_syscall_mask() {
  static const int bsd_syscalls[] = {
    BSC_access,
    BSC_access_extended,
    BSC_chdir,
    BSC_checkuseraccess,
    BSC_chflags,
    BSC_chmod,
    BSC_chmod_extended,
    BSC_chmodat,
    BSC_chown,
    BSC_chownat,
    BSC_chroot,
    BSC_copyfile,
    BSC_delete,
    BSC_dup,
    BSC_dup2,
    BSC_exchangedata,
    BSC_execve,
    BSC_faccessat,
    BSC_fchdir,
    BSC_fchflags,
    BSC_fchmod,
    BSC_fchmod_extended,
    BSC_fchown,
    BSC_fgetattrlist,
    BSC_flock,
    BSC_fremovexattr,
    BSC_fsetattrlist,
    BSC_fsetxattr,
    BSC_fstat,
    BSC_fstat64,
    BSC_fstat64_extended,
    BSC_fstat_extended,
    BSC_fstatat,
    BSC_fstatat64,
    BSC_futimes,
    BSC_getattrlist,
    BSC_getattrlistat,
    BSC_getattrlistbulk,
    BSC_getdirentries,
    BSC_getdirentries64,
    BSC_getdirentriesattr,
    BSC_getxattr,
    BSC_guarded_open_np,
    BSC_lchown,
    BSC_link,
    BSC_linkat,
    BSC_listxattr,
    BSC_lstat,
    BSC_lstat64,
    BSC_lstat64_extended,
    BSC_lstat_extended,
    BSC_mkdir,
    BSC_mkdir_extended,
    BSC_mkdirat,
    BSC_mkfifo,
    BSC_mkfifo_extended,
    BSC_mknod,
    BSC_open,
    BSC_open_dprotected_np,
    BSC_open_extended,
    BSC_open_nocancel,
    BSC_openat,
    BSC_openat_nocancel,
    BSC_pathconf,
    BSC_posix_spawn,
    BSC_pthread_chdir,
    BSC_pthread_fchdir,
    BSC_readlink,
    BSC_readlinkat,
    BSC_removexattr,
    BSC_rename,
    BSC_renameat,
    BSC_rmdir,
    BSC_searchfs,
    BSC_setattrlist,
    BSC_setxattr,
    BSC_stat,
    BSC_stat64,
    BSC_stat64_extended,
    BSC_stat_extended,
    BSC_symlink,
    BSC_symlinkat,
    BSC_truncate,
    BSC_undelete,
    BSC_unlink,
    BSC_unlinkat,
    BSC_utimes,
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
