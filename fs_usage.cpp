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

/*
clang++ -std=c++11 -I/System/Library/Frameworks/System.framework/Versions/B/PrivateHeaders -DPRIVATE -D__APPLE_PRIVATE -arch x86_64 -arch i386 -O -lutil -o fs_usage fs_usage.cpp
*/

#include <array>
#include <tuple>

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <strings.h>
#include <nlist.h>
#include <fcntl.h>
#include <aio.h>
#include <string.h>
#include <dirent.h>
#include <libc.h>
#include <termios.h>
#include <errno.h>
#include <err.h>
#include "libutil.h"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <sys/disk.h>
#include <sys/file.h>

#ifndef KERNEL_PRIVATE
#define KERNEL_PRIVATE
#include "kdebug.h"
#undef KERNEL_PRIVATE
#else
#include "kdebug.h"
#endif /*KERNEL_PRIVATE*/

#import <mach/clock_types.h>
#import <mach/mach_time.h>



#define F_OPENFROM      56              /* SPI: open a file relative to fd (must be a dir) */
#define F_UNLINKFROM    57              /* SPI: open a file relative to fd (must be a dir) */
#define F_CHECK_OPENEVT 58              /* SPI: if a process is marked OPENEVT, or in O_EVTONLY on opens of this vnode */

#define MAXINDEX 2048

#define TEXT_R    0
#define DATA_R    1
#define OBJC_R    2
#define IMPORT_R  3
#define UNICODE_R 4
#define IMAGE_R   5
#define LINKEDIT_R  6

/* 
 * MAXCOLS controls when extra data kicks in.
 * MAX_WIDE_MODE_COLS controls -w mode to get even wider data in path.
 * If NUMPARMS changes to match the kernel, it will automatically
 * get reflected in the -w mode output.
 */
#define NUMPARMS 23
#define PATHLENGTH (NUMPARMS*sizeof(uintptr_t))

#define MAX_WIDE_MODE_COLS (PATHLENGTH + 80)
#define MAXWIDTH MAX_WIDE_MODE_COLS + 64

#define MAX_PATHNAMES   3
#define MAX_SCALL_PATHNAMES 2

typedef struct th_info *th_info_t;

struct lookup {
  uintptr_t pathname[NUMPARMS + 1]; /* add room for null terminator */
};

struct th_info {
  th_info_t next;
  uintptr_t thread;
  uintptr_t child_thread;

  int in_filemgr;
  int in_hfs_update;
  int pid;
  int type;
  int arg1;
  int arg2;
  int arg3;
  int arg4;
  int arg5;
  int arg6;
  int arg7;
  int arg8;
  int waited;
  uint64_t vnodeid;
  const char *nameptr;
  uintptr_t *pathptr;
  int pn_scall_index;
  int pn_work_index;
  struct lookup lookups[MAX_PATHNAMES];
};


typedef struct threadmap * threadmap_t;

struct threadmap {
  threadmap_t tm_next;

  uintptr_t tm_thread;
  unsigned int tm_setsize; /* this is a bit count */
  unsigned long *tm_setptr;  /* file descripter bitmap */
  char tm_command[MAXCOMLEN + 1];
};


typedef struct vnode_info * vnode_info_t;

struct vnode_info {
  vnode_info_t vn_next;
  uint64_t vn_id;
  uintptr_t vn_pathname[NUMPARMS + 1];
};

typedef struct meta_info * meta_info_t;

struct meta_info {
  meta_info_t m_next;
  uint64_t m_blkno;
  const char *m_nameptr;
};

#define HASH_SIZE 1024
#define HASH_MASK (HASH_SIZE - 1)

th_info_t th_info_hash[HASH_SIZE];
th_info_t th_info_freelist;

threadmap_t threadmap_hash[HASH_SIZE];
threadmap_t threadmap_freelist;


#define VN_HASH_SHIFT 3
#define VN_HASH_SIZE 16384
#define VN_HASH_MASK (VN_HASH_SIZE - 1)

vnode_info_t vn_info_hash[VN_HASH_SIZE];
meta_info_t m_info_hash[VN_HASH_SIZE];


int filemgr_in_progress = 0;
int need_new_map = 1;  /* TODO(peck): This should be treated as an error instead. */
long last_time;

int one_good_pid = 0;    /* Used to fail gracefully when bad pids given */
int select_pid_mode = 0;  /* Flag set indicates that output is restricted
            to selected pids or commands */

char *arguments = 0;
int argmax = 0;


#define USLEEP_MIN 1
#define USLEEP_BEHIND 2
#define USLEEP_MAX 32
int usleep_ms = USLEEP_MIN;

/*
 * Filesystem only output filter
 * Default of zero means report all activity - no filtering
 */
#define FILESYS_FILTER 0x01
#define EXEC_FILTER 0x08
#define PATHNAME_FILTER 0x10
#define DEFAULT_DO_NOT_FILTER 0x00

int filter_mode = DEFAULT_DO_NOT_FILTER;

#define NFS_DEV -1
#define CS_DEV -2

extern "C" int reexec_to_match_kernel();

int     check_filter_mode(struct th_info *, int, int, int, const char *);
void    format_print(struct th_info *, const char *, uintptr_t, int, uintptr_t, uintptr_t, uintptr_t, uintptr_t, int, int, const char *);
void    enter_event_now(uintptr_t, int, kd_buf *, const char *);
void    enter_event(uintptr_t thread, int type, kd_buf *kd, const char *name);
void    exit_event(const char *, uintptr_t, int, uintptr_t, uintptr_t, uintptr_t, uintptr_t, int);
void    extend_syscall(uintptr_t, int, kd_buf *);

void    fs_usage_fd_set(uintptr_t, unsigned int);
int     fs_usage_fd_isset(uintptr_t, unsigned int);
void    fs_usage_fd_clear(uintptr_t, unsigned int);

void    init_arguments_buffer();
int     get_real_command_name(int, char *, int);

void    delete_all_events();
void    delete_event(th_info_t);
th_info_t add_event(uintptr_t, int);
th_info_t find_event(uintptr_t, int);

void    read_command_map();
void    delete_all_map_entries();
void    create_map_entry(uintptr_t, int, char *);
void    delete_map_entry(uintptr_t);
threadmap_t find_map_entry(uintptr_t);

char   *add_vnode_name(uint64_t, const char *);
const char *find_vnode_name(uint64_t);
void    add_meta_name(uint64_t, const char *);

void    argtopid(char *str);
void    set_remove();
void    set_pidcheck(int pid, int on_off);
void    set_pidexclude(int pid, int on_off);
int     quit(const char *s);


static constexpr int CLASS_MASK = 0xff000000;
static constexpr int CSC_MASK = 0xffff0000;
#define BSC_INDEX(type) ((type >> 2) & 0x3fff)


static constexpr int MACH_vmfault = 0x01300008;
static constexpr int MACH_pageout = 0x01300004;
static constexpr int VFS_ALIAS_VP = 0x03010094;

static constexpr int BSC_thread_terminate = 0x040c05a4;

static constexpr int HFS_update = 0x3018000;
static constexpr int HFS_modify_block_end = 0x3018004;

static constexpr int Throttled = 0x3010184;
static constexpr int SPEC_ioctl =  0x3060000;
static constexpr int SPEC_unmap_info = 0x3060004;
static constexpr int proc_exit = 0x4010004;

static constexpr int MSC_map_fd = 0x010c00ac;

static constexpr int BSC_BASE = 0x040C0000;
static constexpr int MSC_BASE = 0x010C0000;

// Network related codes
static constexpr int BSC_recvmsg = 0x040C006C;
static constexpr int BSC_sendmsg = 0x040C0070;
static constexpr int BSC_recvfrom = 0x040C0074;
static constexpr int BSC_accept = 0x040C0078;
static constexpr int BSC_select = 0x040C0174;
static constexpr int BSC_socket = 0x040C0184;
static constexpr int BSC_connect = 0x040C0188;
static constexpr int BSC_bind = 0x040C01A0;
static constexpr int BSC_listen = 0x040C01A8;
static constexpr int BSC_sendto = 0x040C0214;
static constexpr int BSC_socketpair = 0x040C021C;
static constexpr int BSC_recvmsg_nocancel = 0x040c0644;
static constexpr int BSC_sendmsg_nocancel = 0x040c0648;
static constexpr int BSC_recvfrom_nocancel = 0x040c064c;
static constexpr int BSC_accept_nocancel = 0x040c0650;
static constexpr int BSC_connect_nocancel = 0x040c0664;
static constexpr int BSC_sendto_nocancel = 0x040c0674;

static constexpr int BSC_exit = 0x040C0004;
static constexpr int BSC_read = 0x040C000C;
static constexpr int BSC_write = 0x040C0010;
static constexpr int BSC_open = 0x040C0014;
static constexpr int BSC_close = 0x040C0018;
static constexpr int BSC_link = 0x040C0024;
static constexpr int BSC_unlink = 0x040C0028;
static constexpr int BSC_chdir = 0x040c0030;
static constexpr int BSC_fchdir = 0x040c0034;
static constexpr int BSC_mknod = 0x040C0038;
static constexpr int BSC_chmod = 0x040C003C;
static constexpr int BSC_chown = 0x040C0040;
static constexpr int BSC_getfsstat = 0x040C0048;
static constexpr int BSC_access = 0x040C0084;
static constexpr int BSC_chflags = 0x040C0088;
static constexpr int BSC_fchflags = 0x040C008C;
static constexpr int BSC_sync = 0x040C0090;
static constexpr int BSC_dup = 0x040C00A4;
static constexpr int BSC_ioctl = 0x040C00D8;
static constexpr int BSC_revoke = 0x040C00E0;
static constexpr int BSC_symlink = 0x040C00E4  ;
static constexpr int BSC_readlink = 0x040C00E8;
static constexpr int BSC_execve = 0x040C00EC;
static constexpr int BSC_umask = 0x040C00F0;
static constexpr int BSC_chroot = 0x040C00F4;
static constexpr int BSC_msync = 0x040C0104;
static constexpr int BSC_dup2 = 0x040C0168;
static constexpr int BSC_fcntl = 0x040C0170;
static constexpr int BSC_fsync = 0x040C017C  ;
static constexpr int BSC_readv = 0x040C01E0  ;
static constexpr int BSC_writev = 0x040C01E4  ;
static constexpr int BSC_fchown = 0x040C01EC  ;
static constexpr int BSC_fchmod = 0x040C01F0  ;
static constexpr int BSC_rename = 0x040C0200;
static constexpr int BSC_flock = 0x040C020C;
static constexpr int BSC_mkfifo = 0x040C0210  ;
static constexpr int BSC_mkdir = 0x040C0220  ;
static constexpr int BSC_rmdir = 0x040C0224;
static constexpr int BSC_utimes = 0x040C0228;
static constexpr int BSC_futimes = 0x040C022C;
static constexpr int BSC_pread = 0x040C0264;
static constexpr int BSC_pwrite = 0x040C0268;
static constexpr int BSC_statfs = 0x040C0274  ;
static constexpr int BSC_fstatfs = 0x040C0278;
static constexpr int BSC_unmount = 0x040C027C;
static constexpr int BSC_mount = 0x040C029C;
static constexpr int BSC_fdatasync = 0x040C02EC;
static constexpr int BSC_stat = 0x040C02F0  ;
static constexpr int BSC_fstat = 0x040C02F4  ;
static constexpr int BSC_lstat = 0x040C02F8  ;
static constexpr int BSC_pathconf = 0x040C02FC  ;
static constexpr int BSC_fpathconf = 0x040C0300;
static constexpr int BSC_getdirentries = 0x040C0310;
static constexpr int BSC_mmap = 0x040c0314;
static constexpr int BSC_lseek = 0x040c031c;
static constexpr int BSC_truncate = 0x040C0320;
static constexpr int BSC_ftruncate = 0x040C0324;
static constexpr int BSC_undelete = 0x040C0334;
static constexpr int BSC_open_dprotected_np = 0x040C0360  ;
static constexpr int BSC_getattrlist = 0x040C0370  ;
static constexpr int BSC_setattrlist = 0x040C0374  ;
static constexpr int BSC_getdirentriesattr = 0x040C0378  ;
static constexpr int BSC_exchangedata = 0x040C037C  ;
static constexpr int BSC_checkuseraccess = 0x040C0380  ;
static constexpr int BSC_searchfs = 0x040C0384;
static constexpr int BSC_delete = 0x040C0388;
static constexpr int BSC_copyfile = 0x040C038C;
static constexpr int BSC_fgetattrlist = 0x040C0390;
static constexpr int BSC_fsetattrlist = 0x040C0394;
static constexpr int BSC_getxattr = 0x040C03A8;
static constexpr int BSC_fgetxattr = 0x040C03AC;
static constexpr int BSC_setxattr = 0x040C03B0;
static constexpr int BSC_fsetxattr = 0x040C03B4;
static constexpr int BSC_removexattr = 0x040C03B8;
static constexpr int BSC_fremovexattr = 0x040C03BC;
static constexpr int BSC_listxattr = 0x040C03C0;
static constexpr int BSC_flistxattr = 0x040C03C4;
static constexpr int BSC_fsctl = 0x040C03C8;
static constexpr int BSC_posix_spawn = 0x040C03D0;
static constexpr int BSC_ffsctl = 0x040C03D4;
static constexpr int BSC_open_extended = 0x040C0454;
static constexpr int BSC_umask_extended = 0x040C0458;
static constexpr int BSC_stat_extended = 0x040C045C;
static constexpr int BSC_lstat_extended = 0x040C0460;
static constexpr int BSC_fstat_extended = 0x040C0464;
static constexpr int BSC_chmod_extended = 0x040C0468;
static constexpr int BSC_fchmod_extended = 0x040C046C;
static constexpr int BSC_access_extended = 0x040C0470;
static constexpr int BSC_mkfifo_extended = 0x040C048C;
static constexpr int BSC_mkdir_extended = 0x040C0490;
static constexpr int BSC_aio_fsync = 0x040C04E4;
static constexpr int BSC_aio_return = 0x040C04E8;
static constexpr int BSC_aio_suspend = 0x040C04EC;
static constexpr int BSC_aio_cancel = 0x040C04F0;
static constexpr int BSC_aio_error = 0x040C04F4;
static constexpr int BSC_aio_read = 0x040C04F8;
static constexpr int BSC_aio_write = 0x040C04FC;
static constexpr int BSC_lio_listio = 0x040C0500;
static constexpr int BSC_sendfile = 0x040C0544;
static constexpr int BSC_stat64 = 0x040C0548;
static constexpr int BSC_fstat64 = 0x040C054C;
static constexpr int BSC_lstat64 = 0x040C0550;
static constexpr int BSC_stat64_extended = 0x040C0554;
static constexpr int BSC_lstat64_extended = 0x040C0558;
static constexpr int BSC_fstat64_extended = 0x040C055C;
static constexpr int BSC_getdirentries64 = 0x040C0560;
static constexpr int BSC_statfs64 = 0x040C0564;
static constexpr int BSC_fstatfs64 = 0x040C0568;
static constexpr int BSC_getfsstat64 = 0x040C056C;
static constexpr int BSC_pthread_chdir = 0x040C0570;
static constexpr int BSC_pthread_fchdir = 0x040C0574;
static constexpr int BSC_lchown = 0x040C05B0;

static constexpr int BSC_read_nocancel = 0x040c0630;
static constexpr int BSC_write_nocancel = 0x040c0634;
static constexpr int BSC_open_nocancel = 0x040c0638;
static constexpr int BSC_close_nocancel = 0x040c063c;
static constexpr int BSC_msync_nocancel = 0x040c0654;
static constexpr int BSC_fcntl_nocancel = 0x040c0658;
static constexpr int BSC_select_nocancel = 0x040c065c;
static constexpr int BSC_fsync_nocancel = 0x040c0660;
static constexpr int BSC_readv_nocancel = 0x040c066c;
static constexpr int BSC_writev_nocancel = 0x040c0670;
static constexpr int BSC_pread_nocancel = 0x040c0678;
static constexpr int BSC_pwrite_nocancel = 0x040c067c;
static constexpr int BSC_aio_suspend_nocancel = 0x40c0694;
static constexpr int BSC_guarded_open_np = 0x040c06e4;
static constexpr int BSC_guarded_close_np = 0x040c06e8;

static constexpr int BSC_fsgetpath = 0x040c06ac;

static constexpr int BSC_getattrlistbulk = 0x040c0734;

static constexpr int BSC_openat = 0x040c073c;
static constexpr int BSC_openat_nocancel = 0x040c0740;
static constexpr int BSC_renameat = 0x040c0744;
static constexpr int BSC_chmodat = 0x040c074c;
static constexpr int BSC_chownat = 0x040c0750;
static constexpr int BSC_fstatat = 0x040c0754;
static constexpr int BSC_fstatat64 = 0x040c0758;
static constexpr int BSC_linkat = 0x040c075c;
static constexpr int BSC_unlinkat = 0x040c0760;
static constexpr int BSC_readlinkat = 0x040c0764;
static constexpr int BSC_symlinkat = 0x040c0768;
static constexpr int BSC_mkdirat = 0x040c076c;
static constexpr int BSC_getattrlistat = 0x040c0770;

static constexpr int BSC_msync_extended = 0x040e0104;
static constexpr int BSC_pread_extended = 0x040e0264;
static constexpr int BSC_pwrite_extended = 0x040e0268;
static constexpr int BSC_mmap_extended = 0x040e0314;
static constexpr int BSC_mmap_extended2 = 0x040f0314;

// Carbon File Manager support
static constexpr int FILEMGR_PBGETCATALOGINFO = 0x1e000020;
static constexpr int FILEMGR_PBGETCATALOGINFOBULK = 0x1e000024;
static constexpr int FILEMGR_PBCREATEFILEUNICODE = 0x1e000028;
static constexpr int FILEMGR_PBCREATEDIRECTORYUNICODE = 0x1e00002c;
static constexpr int FILEMGR_PBCREATEFORK = 0x1e000030;
static constexpr int FILEMGR_PBDELETEFORK = 0x1e000034;
static constexpr int FILEMGR_PBITERATEFORK = 0x1e000038;
static constexpr int FILEMGR_PBOPENFORK = 0x1e00003c;
static constexpr int FILEMGR_PBREADFORK = 0x1e000040;
static constexpr int FILEMGR_PBWRITEFORK = 0x1e000044;
static constexpr int FILEMGR_PBALLOCATEFORK = 0x1e000048;
static constexpr int FILEMGR_PBDELETEOBJECT = 0x1e00004c;
static constexpr int FILEMGR_PBEXCHANGEOBJECT = 0x1e000050;
static constexpr int FILEMGR_PBGETFORKCBINFO = 0x1e000054;
static constexpr int FILEMGR_PBGETVOLUMEINFO = 0x1e000058;
static constexpr int FILEMGR_PBMAKEFSREF = 0x1e00005c;
static constexpr int FILEMGR_PBMAKEFSREFUNICODE = 0x1e000060;
static constexpr int FILEMGR_PBMOVEOBJECT = 0x1e000064;
static constexpr int FILEMGR_PBOPENITERATOR = 0x1e000068;
static constexpr int FILEMGR_PBRENAMEUNICODE = 0x1e00006c;
static constexpr int FILEMGR_PBSETCATALOGINFO = 0x1e000070;
static constexpr int FILEMGR_PBSETVOLUMEINFO = 0x1e000074;
static constexpr int FILEMGR_FSREFMAKEPATH = 0x1e000078;
static constexpr int FILEMGR_FSPATHMAKEREF = 0x1e00007c;

static constexpr int FILEMGR_PBGETCATINFO = 0x1e010000;
static constexpr int FILEMGR_PBGETCATINFOLITE = 0x1e010004;
static constexpr int FILEMGR_PBHGETFINFO = 0x1e010008;
static constexpr int FILEMGR_PBXGETVOLINFO = 0x1e01000c;
static constexpr int FILEMGR_PBHCREATE = 0x1e010010;
static constexpr int FILEMGR_PBHOPENDF = 0x1e010014;
static constexpr int FILEMGR_PBHOPENRF = 0x1e010018;
static constexpr int FILEMGR_PBHGETDIRACCESS = 0x1e01001c;
static constexpr int FILEMGR_PBHSETDIRACCESS = 0x1e010020;
static constexpr int FILEMGR_PBHMAPID = 0x1e010024;
static constexpr int FILEMGR_PBHMAPNAME = 0x1e010028;
static constexpr int FILEMGR_PBCLOSE = 0x1e01002c;
static constexpr int FILEMGR_PBFLUSHFILE = 0x1e010030;
static constexpr int FILEMGR_PBGETEOF = 0x1e010034;
static constexpr int FILEMGR_PBSETEOF = 0x1e010038;
static constexpr int FILEMGR_PBGETFPOS = 0x1e01003c;
static constexpr int FILEMGR_PBREAD = 0x1e010040;
static constexpr int FILEMGR_PBWRITE = 0x1e010044;
static constexpr int FILEMGR_PBGETFCBINFO = 0x1e010048;
static constexpr int FILEMGR_PBSETFINFO = 0x1e01004c;
static constexpr int FILEMGR_PBALLOCATE = 0x1e010050;
static constexpr int FILEMGR_PBALLOCCONTIG = 0x1e010054;
static constexpr int FILEMGR_PBSETFPOS = 0x1e010058;
static constexpr int FILEMGR_PBSETCATINFO = 0x1e01005c;
static constexpr int FILEMGR_PBGETVOLPARMS = 0x1e010060;
static constexpr int FILEMGR_PBSETVINFO = 0x1e010064;
static constexpr int FILEMGR_PBMAKEFSSPEC = 0x1e010068;
static constexpr int FILEMGR_PBHGETVINFO = 0x1e01006c;
static constexpr int FILEMGR_PBCREATEFILEIDREF = 0x1e010070;
static constexpr int FILEMGR_PBDELETEFILEIDREF = 0x1e010074;
static constexpr int FILEMGR_PBRESOLVEFILEIDREF = 0x1e010078;
static constexpr int FILEMGR_PBFLUSHVOL = 0x1e01007c;
static constexpr int FILEMGR_PBHRENAME = 0x1e010080;
static constexpr int FILEMGR_PBCATMOVE = 0x1e010084;
static constexpr int FILEMGR_PBEXCHANGEFILES = 0x1e010088;
static constexpr int FILEMGR_PBHDELETE = 0x1e01008c;
static constexpr int FILEMGR_PBDIRCREATE = 0x1e010090;
static constexpr int FILEMGR_PBCATSEARCH = 0x1e010094;
static constexpr int FILEMGR_PBHSETFLOCK = 0x1e010098;
static constexpr int FILEMGR_PBHRSTFLOCK = 0x1e01009c;
static constexpr int FILEMGR_PBLOCKRANGE = 0x1e0100a0;
static constexpr int FILEMGR_PBUNLOCKRANGE = 0x1e0100a4;


static constexpr int FILEMGR_CLASS = 0x1e;
static constexpr int FILEMGR_BASE = 0x1e000000;

#define FMT_DEFAULT 0
#define FMT_FD    1
#define FMT_FD_IO 2
#define FMT_FD_2  3
#define FMT_SOCKET  4
#define FMT_LSEEK 9
#define FMT_PREAD 10
#define FMT_FTRUNC  11
#define FMT_TRUNC 12
#define FMT_SELECT  13
#define FMT_OPEN  14
#define FMT_AIO_FSYNC 15
#define FMT_AIO_RETURN  16
#define FMT_AIO_SUSPEND 17
#define FMT_AIO_CANCEL  18
#define FMT_AIO   19
#define FMT_LIO_LISTIO  20
#define FMT_MSYNC 21
#define FMT_FCNTL 22
#define FMT_ACCESS  23
#define FMT_CHMOD 24
#define FMT_FCHMOD  25
#define FMT_CHMOD_EXT 26
#define FMT_FCHMOD_EXT  27
#define FMT_CHFLAGS 28
#define FMT_FCHFLAGS  29
#define FMT_IOCTL 30
#define FMT_MMAP  31
#define FMT_UMASK 32
#define FMT_SENDFILE  33
#define FMT_IOCTL_SYNC  34
#define FMT_MOUNT 35
#define FMT_UNMOUNT 36
#define FMT_IOCTL_UNMAP 39
#define FMT_UNMAP_INFO  40
#define FMT_HFS_update  41
#define FMT_FLOCK 42
#define FMT_AT    43
#define FMT_CHMODAT 44
#define FMT_OPENAT  45
#define FMT_RENAMEAT  46
#define FMT_IOCTL_SYNCCACHE 47


struct bsd_syscall {
  static_assert(FMT_DEFAULT == 0, "bsd_syscall should be all zeros by default");
  const char *sc_name = nullptr;
  int sc_format = FMT_DEFAULT;
};

static constexpr int MAX_BSD_SYSCALL = 526;

std::array<bsd_syscall, MAX_BSD_SYSCALL> make_bsd_syscall_table() {
  static const std::tuple<int, const char *, int> bsd_syscall_table[] = {
    { BSC_sendfile, "sendfile", FMT_FD /* this should be changed to FMT_SENDFILE once we add an extended info trace event */ },
    { BSC_recvmsg, "recvmsg", FMT_FD_IO },
    { BSC_recvmsg_nocancel, "recvmsg", FMT_FD_IO },
    { BSC_sendmsg, "sendmsg", FMT_FD_IO },
    { BSC_sendmsg_nocancel, "sendmsg", FMT_FD_IO },
    { BSC_recvfrom, "recvfrom", FMT_FD_IO },
    { BSC_recvfrom_nocancel, "recvfrom", FMT_FD_IO },
    { BSC_sendto, "sendto", FMT_FD_IO },
    { BSC_sendto_nocancel, "sendto", FMT_FD_IO },
    { BSC_select, "select", FMT_SELECT },
    { BSC_select_nocancel, "select", FMT_SELECT },
    { BSC_accept, "accept", FMT_FD_2 },
    { BSC_accept_nocancel, "accept", FMT_FD_2 },
    { BSC_socket, "socket", FMT_SOCKET },
    { BSC_connect, "connect", FMT_FD },
    { BSC_connect_nocancel, "connect", FMT_FD },
    { BSC_bind, "bind", FMT_FD },
    { BSC_listen, "listen", FMT_FD },
    { BSC_mmap, "mmap", FMT_MMAP },
    { BSC_socketpair, "socketpair", FMT_DEFAULT },
    { BSC_getxattr, "getxattr", FMT_DEFAULT },
    { BSC_setxattr, "setxattr", FMT_DEFAULT },
    { BSC_removexattr, "removexattr", FMT_DEFAULT },
    { BSC_listxattr, "listxattr", FMT_DEFAULT },
    { BSC_stat, "stat", FMT_DEFAULT },
    { BSC_stat64, "stat64", FMT_DEFAULT },
    { BSC_stat_extended, "stat_extended", FMT_DEFAULT },
    { BSC_stat64_extended, "stat_extended64", FMT_DEFAULT },
    { BSC_mount, "mount", FMT_MOUNT },
    { BSC_unmount, "unmount", FMT_UNMOUNT },
    { BSC_exit, "exit", FMT_DEFAULT },
    { BSC_execve, "execve", FMT_DEFAULT },
    { BSC_posix_spawn, "posix_spawn", FMT_DEFAULT },
    { BSC_open, "open", FMT_OPEN },
    { BSC_open_nocancel, "open", FMT_OPEN },
    { BSC_open_extended, "open_extended", FMT_OPEN },
    { BSC_guarded_open_np, "guarded_open_np", FMT_OPEN },
    { BSC_open_dprotected_np, "open_dprotected", FMT_OPEN },
    { BSC_dup, "dup", FMT_FD_2 },
    { BSC_dup2, "dup2", FMT_FD_2 },
    { BSC_close, "close", FMT_FD },
    { BSC_close_nocancel, "close", FMT_FD },
    { BSC_guarded_close_np, "guarded_close_np", FMT_FD },
    { BSC_read, "read", FMT_FD_IO },
    { BSC_read_nocancel, "read", FMT_FD_IO },
    { BSC_write, "write", FMT_FD_IO },
    { BSC_write_nocancel, "write", FMT_FD_IO },
    { BSC_fgetxattr, "fgetxattr", FMT_FD },
    { BSC_fsetxattr, "fsetxattr", FMT_FD },
    { BSC_fremovexattr, "fremovexattr", FMT_FD },
    { BSC_flistxattr, "flistxattr", FMT_FD },
    { BSC_fstat, "fstat", FMT_FD },
    { BSC_fstat64, "fstat64", FMT_FD },
    { BSC_fstat_extended, "fstat_extended", FMT_FD },
    { BSC_fstat64_extended, "fstat64_extended", FMT_FD },
    { BSC_lstat, "lstat", FMT_DEFAULT },
    { BSC_lstat64, "lstat64", FMT_DEFAULT },
    { BSC_lstat_extended, "lstat_extended", FMT_DEFAULT },
    { BSC_lstat64_extended, "lstat_extended64", FMT_DEFAULT },
    { BSC_link, "link", FMT_DEFAULT },
    { BSC_unlink, "unlink", FMT_DEFAULT },
    { BSC_mknod, "mknod", FMT_DEFAULT },
    { BSC_umask, "umask", FMT_UMASK },
    { BSC_umask_extended, "umask_extended", FMT_UMASK },
    { BSC_chmod, "chmod", FMT_CHMOD },
    { BSC_chmod_extended, "chmod_extended", FMT_CHMOD_EXT },
    { BSC_fchmod, "fchmod", FMT_FCHMOD },
    { BSC_fchmod_extended, "fchmod_extended", FMT_FCHMOD_EXT },
    { BSC_chown, "chown", FMT_DEFAULT },
    { BSC_lchown, "lchown", FMT_DEFAULT },
    { BSC_fchown, "fchown", FMT_FD },
    { BSC_access, "access", FMT_ACCESS },
    { BSC_access_extended, "access_extended", FMT_DEFAULT },
    { BSC_chdir, "chdir", FMT_DEFAULT },
    { BSC_pthread_chdir, "pthread_chdir", FMT_DEFAULT },
    { BSC_chroot, "chroot", FMT_DEFAULT },
    { BSC_utimes, "utimes", FMT_DEFAULT },
    { BSC_delete, "delete-Carbon", FMT_DEFAULT },
    { BSC_undelete, "undelete", FMT_DEFAULT },
    { BSC_revoke, "revoke", FMT_DEFAULT },
    { BSC_fsctl, "fsctl", FMT_DEFAULT },
    { BSC_ffsctl, "ffsctl", FMT_FD },
    { BSC_chflags, "chflags", FMT_CHFLAGS },
    { BSC_fchflags, "fchflags", FMT_FCHFLAGS },
    { BSC_fchdir, "fchdir", FMT_FD },
    { BSC_pthread_fchdir, "pthread_fchdir", FMT_FD },
    { BSC_futimes, "futimes", FMT_FD },
    { BSC_sync, "sync", FMT_DEFAULT },
    { BSC_symlink, "symlink", FMT_DEFAULT },
    { BSC_readlink, "readlink", FMT_DEFAULT },
    { BSC_fsync, "fsync", FMT_FD },
    { BSC_fsync_nocancel, "fsync", FMT_FD },
    { BSC_fdatasync, "fdatasync", FMT_FD },
    { BSC_readv, "readv", FMT_FD_IO },
    { BSC_readv_nocancel, "readv", FMT_FD_IO },
    { BSC_writev, "writev", FMT_FD_IO },
    { BSC_writev_nocancel, "writev", FMT_FD_IO },
    { BSC_pread, "pread", FMT_PREAD },
    { BSC_pread_nocancel, "pread", FMT_PREAD },
    { BSC_pwrite, "pwrite", FMT_PREAD },
    { BSC_pwrite_nocancel, "pwrite", FMT_PREAD },
    { BSC_mkdir, "mkdir", FMT_DEFAULT },
    { BSC_mkdir_extended, "mkdir_extended", FMT_DEFAULT },
    { BSC_mkfifo, "mkfifo", FMT_DEFAULT },
    { BSC_mkfifo_extended, "mkfifo_extended", FMT_DEFAULT },
    { BSC_rmdir, "rmdir", FMT_DEFAULT },
    { BSC_statfs, "statfs", FMT_DEFAULT },
    { BSC_statfs64, "statfs64", FMT_DEFAULT },
    { BSC_getfsstat, "getfsstat", FMT_DEFAULT },
    { BSC_getfsstat64, "getfsstat64", FMT_DEFAULT },
    { BSC_fstatfs, "fstatfs", FMT_FD },
    { BSC_fstatfs64, "fstatfs64", FMT_FD },
    { BSC_pathconf, "pathconf", FMT_DEFAULT },
    { BSC_fpathconf, "fpathconf", FMT_FD },
    { BSC_getdirentries, "getdirentries", FMT_FD_IO },
    { BSC_getdirentries64, "getdirentries64", FMT_FD_IO },
    { BSC_lseek, "lseek", FMT_LSEEK },
    { BSC_truncate, "truncate", FMT_TRUNC },
    { BSC_ftruncate, "ftruncate", FMT_FTRUNC },
    { BSC_flock, "flock", FMT_FLOCK },
    { BSC_getattrlist, "getattrlist", FMT_DEFAULT },
    { BSC_setattrlist, "setattrlist", FMT_DEFAULT },
    { BSC_fgetattrlist, "fgetattrlist", FMT_FD },
    { BSC_fsetattrlist, "fsetattrlist", FMT_FD },
    { BSC_getdirentriesattr, "getdirentriesattr", FMT_FD },
    { BSC_exchangedata, "exchangedata", FMT_DEFAULT },
    { BSC_rename, "rename", FMT_DEFAULT },
    { BSC_copyfile, "copyfile", FMT_DEFAULT },
    { BSC_checkuseraccess, "checkuseraccess", FMT_DEFAULT },
    { BSC_searchfs, "searchfs", FMT_DEFAULT },
    { BSC_aio_fsync, "aio_fsync", FMT_AIO_FSYNC },
    { BSC_aio_return, "aio_return", FMT_AIO_RETURN },
    { BSC_aio_suspend, "aio_suspend", FMT_AIO_SUSPEND },
    { BSC_aio_suspend_nocancel, "aio_suspend", FMT_AIO_SUSPEND },
    { BSC_aio_cancel,  "aio_cancel", FMT_AIO_CANCEL },
    { BSC_aio_error, "aio_error", FMT_AIO },
    { BSC_aio_read, "aio_read", FMT_AIO },
    { BSC_aio_write, "aio_write", FMT_AIO },
    { BSC_lio_listio, "lio_listio", FMT_LIO_LISTIO },
    { BSC_msync, "msync", FMT_MSYNC },
    { BSC_msync_nocancel, "msync", FMT_MSYNC },
    { BSC_fcntl, "fcntl", FMT_FCNTL },
    { BSC_fcntl_nocancel, "fcntl", FMT_FCNTL },
    { BSC_ioctl, "ioctl", FMT_IOCTL },
    { BSC_fsgetpath, "fsgetpath", FMT_DEFAULT },
    { BSC_getattrlistbulk, "getattrlistbulk", FMT_DEFAULT },
    { BSC_openat, "openat", FMT_OPENAT },
    { BSC_openat_nocancel, "openat", FMT_OPENAT },
    { BSC_renameat, "renameat", FMT_RENAMEAT },
    { BSC_chmodat, "chmodat", FMT_CHMODAT },
    { BSC_chownat, "chownat", FMT_AT },
    { BSC_fstatat, "fstatat", FMT_AT },
    { BSC_fstatat64, "fstatat64", FMT_AT },
    { BSC_linkat, "linkat", FMT_AT },
    { BSC_unlinkat, "unlinkat", FMT_AT },
    { BSC_readlinkat, "readlinkat", FMT_AT },
    { BSC_symlinkat, "symlinkat", FMT_AT },
    { BSC_mkdirat, "mkdirat", FMT_AT },
    { BSC_getattrlistat, "getattrlistat", FMT_AT },
  };

  std::array<bsd_syscall, MAX_BSD_SYSCALL> result;
  for (auto syscall_descriptor : bsd_syscall_table) {
    int code = BSC_INDEX(std::get<0>(syscall_descriptor));

    auto &syscall = result.at(code);
    syscall.sc_name = std::get<1>(syscall_descriptor);
    syscall.sc_format = std::get<2>(syscall_descriptor);
  }
  return result;
}

static const auto bsd_syscalls = make_bsd_syscall_table();

#define MAX_FILEMGR 512

int filemgr_index(int type) {
  if (type & 0x10000) {
    return (((type >> 2) & 0x3fff) + 256);
  }

  return (((type >> 2) & 0x3fff));
}

struct filemgr_call {
  const char *fm_name = nullptr;
};

std::array<filemgr_call, MAX_FILEMGR> make_filemgr_calls() {
  static const std::pair<int, const char *> filemgr_call_types[] = {
    { FILEMGR_PBGETCATALOGINFO, "GetCatalogInfo" },
    { FILEMGR_PBGETCATALOGINFOBULK, "GetCatalogInfoBulk" },
    { FILEMGR_PBCREATEFILEUNICODE, "CreateFileUnicode" },
    { FILEMGR_PBCREATEDIRECTORYUNICODE, "CreateDirectoryUnicode" },
    { FILEMGR_PBCREATEFORK, "PBCreateFork" },
    { FILEMGR_PBDELETEFORK, "PBDeleteFork" },
    { FILEMGR_PBITERATEFORK, "PBIterateFork" },
    { FILEMGR_PBOPENFORK, "PBOpenFork" },
    { FILEMGR_PBREADFORK, "PBReadFork" },
    { FILEMGR_PBWRITEFORK, "PBWriteFork" },
    { FILEMGR_PBALLOCATEFORK, "PBAllocateFork" },
    { FILEMGR_PBDELETEOBJECT, "PBDeleteObject" },
    { FILEMGR_PBEXCHANGEOBJECT, "PBExchangeObject" },
    { FILEMGR_PBGETFORKCBINFO, "PBGetForkCBInfo" },
    { FILEMGR_PBGETVOLUMEINFO, "PBGetVolumeInfo" },
    { FILEMGR_PBMAKEFSREF, "PBMakeFSRef" },
    { FILEMGR_PBMAKEFSREFUNICODE, "PBMakeFSRefUnicode" },
    { FILEMGR_PBMOVEOBJECT, "PBMoveObject" },
    { FILEMGR_PBOPENITERATOR, "PBOpenIterator" },
    { FILEMGR_PBRENAMEUNICODE, "PBRenameUnicode" },
    { FILEMGR_PBSETCATALOGINFO, "SetCatalogInfo" },
    { FILEMGR_PBSETVOLUMEINFO, "SetVolumeInfo" },
    { FILEMGR_FSREFMAKEPATH, "FSRefMakePath" },
    { FILEMGR_FSPATHMAKEREF, "FSPathMakeRef" },
    { FILEMGR_PBGETCATINFO, "GetCatInfo" },
    { FILEMGR_PBGETCATINFOLITE, "GetCatInfoLite" },
    { FILEMGR_PBHGETFINFO, "PBHGetFInfo" },
    { FILEMGR_PBXGETVOLINFO, "PBXGetVolInfo" },
    { FILEMGR_PBHCREATE, "PBHCreate" },
    { FILEMGR_PBHOPENDF, "PBHOpenDF" },
    { FILEMGR_PBHOPENRF, "PBHOpenRF" },
    { FILEMGR_PBHGETDIRACCESS, "PBHGetDirAccess" },
    { FILEMGR_PBHSETDIRACCESS, "PBHSetDirAccess" },
    { FILEMGR_PBHMAPID, "PBHMapID" },
    { FILEMGR_PBHMAPNAME, "PBHMapName" },
    { FILEMGR_PBCLOSE, "PBClose" },
    { FILEMGR_PBFLUSHFILE, "PBFlushFile" },
    { FILEMGR_PBGETEOF, "PBGetEOF" },
    { FILEMGR_PBSETEOF, "PBSetEOF" },
    { FILEMGR_PBGETFPOS, "PBGetFPos" },
    { FILEMGR_PBREAD, "PBRead" },
    { FILEMGR_PBWRITE, "PBWrite" },
    { FILEMGR_PBGETFCBINFO, "PBGetFCBInfo" },
    { FILEMGR_PBSETFINFO, "PBSetFInfo" },
    { FILEMGR_PBALLOCATE, "PBAllocate" },
    { FILEMGR_PBALLOCCONTIG, "PBAllocContig" },
    { FILEMGR_PBSETFPOS, "PBSetFPos" },
    { FILEMGR_PBSETCATINFO, "PBSetCatInfo" },
    { FILEMGR_PBGETVOLPARMS, "PBGetVolParms" },
    { FILEMGR_PBSETVINFO, "PBSetVInfo" },
    { FILEMGR_PBMAKEFSSPEC, "PBMakeFSSpec" },
    { FILEMGR_PBHGETVINFO, "PBHGetVInfo" },
    { FILEMGR_PBCREATEFILEIDREF, "PBCreateFileIDRef" },
    { FILEMGR_PBDELETEFILEIDREF, "PBDeleteFileIDRef" },
    { FILEMGR_PBRESOLVEFILEIDREF, "PBResolveFileIDRef" },
    { FILEMGR_PBFLUSHVOL, "PBFlushVol" },
    { FILEMGR_PBHRENAME, "PBHRename" },
    { FILEMGR_PBCATMOVE, "PBCatMove" },
    { FILEMGR_PBEXCHANGEFILES, "PBExchangeFiles" },
    { FILEMGR_PBHDELETE, "PBHDelete" },
    { FILEMGR_PBDIRCREATE, "PBDirCreate" },
    { FILEMGR_PBCATSEARCH, "PBCatSearch" },
    { FILEMGR_PBHSETFLOCK, "PBHSetFlock" },
    { FILEMGR_PBHRSTFLOCK, "PBHRstFLock" },
    { FILEMGR_PBLOCKRANGE, "PBLockRange" },
    { FILEMGR_PBUNLOCKRANGE, "PBUnlockRange" }
  };

  std::array<filemgr_call, MAX_FILEMGR> result{};
  for (auto filemgr_call_type : filemgr_call_types) {
    int code = filemgr_index(filemgr_call_type.first);
    result[code].fm_name = filemgr_call_type.second;
  }
  return result;
}

static auto filemgr_calls = make_filemgr_calls();


std::array<int, 256> pids;

int num_of_pids = 0;
int exclude_pids = 0;


struct kinfo_proc *kp_buffer = 0;
int kp_nentries = 0;

#define EVENT_BASE 60000

int num_events = EVENT_BASE;


#define DBG_FUNC_ALL  (DBG_FUNC_START | DBG_FUNC_END)
#define DBG_FUNC_MASK 0xfffffffc

int mib[6];
size_t needed;
char *my_buffer;

kbufinfo_t bufinfo = {0, 0, 0, 0, 0};


/* defines for tracking file descriptor state */
#define FS_USAGE_FD_SETSIZE 256   /* Initial number of file descriptors per
             thread that we will track */

#define FS_USAGE_NFDBITS      (sizeof (unsigned long) * 8)
#define FS_USAGE_NFDBYTES(n)  (((n) / FS_USAGE_NFDBITS) * sizeof (unsigned long))

int trace_enabled = 0;
int set_remove_flag = 1;

void set_numbufs(int nbufs);
void set_filter();
void set_init();
void set_enable(int val);
void sample_sc();

/*
 *  signal handlers
 */

void leave(int sig) {      /* exit under normal conditions -- INT handler */
  fflush(0);

  set_enable(0);

  if (exclude_pids == 0) {
    for (int i = 0; i < num_of_pids; i++) {
      set_pidcheck(pids[i], 0);
    }
  } else {
    for (int i = 0; i < num_of_pids; i++) {
      set_pidexclude(pids[i], 0);
    }
  }
  set_remove();

  exit(0);
}


int quit(const char *s) {
  if (trace_enabled) {
    set_enable(0);
  }

  /* 
   * This flag is turned off when calling
   * quit() due to a set_remove() failure.
   */
  if (set_remove_flag) {
    set_remove();
  }

  fprintf(stderr, "fs_usage: ");
  if (s) {
    fprintf(stderr, "%s", s);
  }

  exit(1);
}

int exit_usage(const char *myname) {

  fprintf(stderr, "Usage: %s [-e] [-f mode] [pid | cmd [pid | cmd] ...]\n", myname);
  fprintf(stderr, "  -e    exclude the specified list of pids from the sample\n");
  fprintf(stderr, "        and exclude fs_usage by default\n");
  fprintf(stderr, "  -f    output is based on the mode provided\n");
  fprintf(stderr, "          mode = \"filesys\"  Show filesystem-related events\n");
  fprintf(stderr, "          mode = \"pathname\" Show only pathname-related events\n");
  fprintf(stderr, "          mode = \"exec\"     Show only exec and spawn events\n");
  fprintf(stderr, "  pid   selects process(s) to sample\n");
  fprintf(stderr, "  cmd   selects process(s) matching command string to sample\n");
  fprintf(stderr, "\n%s will handle a maximum list of %zu pids.\n\n", myname, pids.size());
  fprintf(stderr, "By default (no options) the following processes are excluded from the output:\n");
  fprintf(stderr, "fs_usage, Terminal, telnetd, sshd, rlogind, tcsh, csh, sh\n\n");

  exit(1);
}



int main(int argc, char *argv[]) {
  const char *myname = "fs_usage";

  if (0 != reexec_to_match_kernel()) {
    fprintf(stderr, "Could not re-execute: %d\n", errno);
    exit(1);
  }

  /*
   * get our name
   */
  if (argc > 0) {
    if ((myname = rindex(argv[0], '/')) == 0) {
      myname = argv[0];
    } else {
      myname++;
    }
  }
  
  for (char ch; (ch = getopt(argc, argv, "bewf:R:S:E:t:W")) != EOF;) {

    switch(ch) {
      case 'e':
        exclude_pids = 1;
        break;

      case 'f':
        if (!strcmp(optarg, "filesys")) {
          filter_mode |= FILESYS_FILTER;
        } else if (!strcmp(optarg, "exec")) {
          filter_mode |= EXEC_FILTER;
        } else if (!strcmp(optarg, "pathname")) {
          filter_mode |= PATHNAME_FILTER;
        }
        break;

    default:
      exit_usage(myname);     
    }
  }
  if (geteuid() != 0) {
    fprintf(stderr, "'fs_usage' must be run as root...\n");
    exit(1);
  }
  argc -= optind;
  argv += optind;

  /*
   * when excluding, fs_usage should be the first in line for pids[]
   */
  if (exclude_pids || (!exclude_pids && argc == 0)) {
    if (num_of_pids < (pids.size() - 1)) {
      pids[num_of_pids++] = getpid();
    }
  }

  while (argc > 0 && num_of_pids < (pids.size() - 1)) {
    select_pid_mode++;
    argtopid(argv[0]);
    argc--;
    argv++;
  }
  struct sigaction osa;
  int num_cpus;
  size_t  len;

  /* set up signal handlers */
  signal(SIGINT, leave);
  signal(SIGQUIT, leave);
  signal(SIGPIPE, leave);

  sigaction(SIGHUP, (struct sigaction *)NULL, &osa);

  if (osa.sa_handler == SIG_DFL) {
    signal(SIGHUP, leave);
  }
  signal(SIGTERM, leave);
  /*
   * grab the number of cpus
   */
  mib[0] = CTL_HW;
  mib[1] = HW_NCPU;
  mib[2] = 0;
  len = sizeof(num_cpus);

  sysctl(mib, 2, &num_cpus, &len, NULL, 0);
  num_events = EVENT_BASE * num_cpus;

  if ((my_buffer = reinterpret_cast<char *>(malloc(num_events * sizeof(kd_buf)))) == (char *)0) {
    quit("can't allocate memory for tracing info\n");
  }

  set_remove();
  set_numbufs(num_events);
  set_init();

  if (exclude_pids == 0) {
    for (int i = 0; i < num_of_pids; i++) {
      set_pidcheck(pids[i], 1);
    }
  } else {
    for (int i = 0; i < num_of_pids; i++) {
      set_pidexclude(pids[i], 1);
    }
  }
  if (select_pid_mode && !one_good_pid) {
    /*
     *  An attempt to restrict output to a given
     *  pid or command has failed. Exit gracefully
     */
    set_remove();
    exit_usage(myname);
  }

  set_filter();

  set_enable(1);

  init_arguments_buffer();

  /*
   * main loop
   */
  for (;;) {
    usleep(1000 * usleep_ms);

    sample_sc();

    last_time = time((long *)0);
  }
}


void find_proc_names() {
  size_t bufSize = 0;
  struct kinfo_proc *kp;

  mib[0] = CTL_KERN;
  mib[1] = KERN_PROC;
  mib[2] = KERN_PROC_ALL;
  mib[3] = 0;

  if (sysctl(mib, 4, NULL, &bufSize, NULL, 0) < 0) {
    quit("trace facility failure, KERN_PROC_ALL\n");
  }

  if ((kp = (struct kinfo_proc *)malloc(bufSize)) == (struct kinfo_proc *)0) {
    quit("can't allocate memory for proc buffer\n");
  }
  
  if (sysctl(mib, 4, kp, &bufSize, NULL, 0) < 0) {
    quit("trace facility failure, KERN_PROC_ALL\n");

    kp_nentries = bufSize/ sizeof(struct kinfo_proc);
  }
  kp_buffer = kp;
}


void set_enable(int val) {
  mib[0] = CTL_KERN;
  mib[1] = KERN_KDEBUG;
  mib[2] = KERN_KDENABLE;   /* protocol */
  mib[3] = val;
  mib[4] = 0;
  mib[5] = 0;           /* no flags */

  if (sysctl(mib, 4, NULL, &needed, NULL, 0) < 0) {
    quit("trace facility failure, KERN_KDENABLE\n");
  }

  if (val) {
    trace_enabled = 1;
  } else {
    trace_enabled = 0;
  }
}

void set_numbufs(int nbufs) {
  mib[0] = CTL_KERN;
  mib[1] = KERN_KDEBUG;
  mib[2] = KERN_KDSETBUF;
  mib[3] = nbufs;
  mib[4] = 0;
  mib[5] = 0;           /* no flags */

  if (sysctl(mib, 4, NULL, &needed, NULL, 0) < 0) {
    quit("trace facility failure, KERN_KDSETBUF\n");
  }

  mib[0] = CTL_KERN;
  mib[1] = KERN_KDEBUG;
  mib[2] = KERN_KDSETUP;    
  mib[3] = 0;
  mib[4] = 0;
  mib[5] = 0;           /* no flags */

  if (sysctl(mib, 3, NULL, &needed, NULL, 0) < 0) {
    quit("trace facility failure, KERN_KDSETUP\n");
  }
}

#define ENCODE_CSC_LOW(klass, subclass) \
  ( (uint16_t) ( ((klass) & 0xff) << 8 ) | ((subclass) & 0xff) )

void set_filter() {
  uint8_t type_filter_bitmap[KDBG_TYPEFILTER_BITMAP_SIZE];
  bzero(type_filter_bitmap, sizeof(type_filter_bitmap));

  setbit(type_filter_bitmap, ENCODE_CSC_LOW(DBG_TRACE,DBG_TRACE_DATA));
  setbit(type_filter_bitmap, ENCODE_CSC_LOW(DBG_TRACE,DBG_TRACE_STRING));

  setbit(type_filter_bitmap, ENCODE_CSC_LOW(DBG_MACH,DBG_MACH_EXCP_SC)); //0x010c

  setbit(type_filter_bitmap, ENCODE_CSC_LOW(DBG_FSYSTEM,DBG_FSRW)); //0x0301
  setbit(type_filter_bitmap, ENCODE_CSC_LOW(DBG_FSYSTEM,DBG_IOCTL)); //0x0306
  setbit(type_filter_bitmap, ENCODE_CSC_LOW(DBG_FSYSTEM,DBG_BOOTCACHE)); //0x0307

  setbit(type_filter_bitmap, ENCODE_CSC_LOW(DBG_BSD,DBG_BSD_EXCP_SC)); //0x040c
  setbit(type_filter_bitmap, ENCODE_CSC_LOW(DBG_BSD,DBG_BSD_PROC)); //0x0401
  setbit(type_filter_bitmap, ENCODE_CSC_LOW(DBG_BSD,DBG_BSD_SC_EXTENDED_INFO)); //0x040e
  setbit(type_filter_bitmap, ENCODE_CSC_LOW(DBG_BSD,DBG_BSD_SC_EXTENDED_INFO2)); //0x040f

  setbit(type_filter_bitmap, ENCODE_CSC_LOW(FILEMGR_CLASS, 0)); //Carbon File Manager
  setbit(type_filter_bitmap, ENCODE_CSC_LOW(FILEMGR_CLASS, 1)); //Carbon File Manager

  errno = 0;
  int mib[] = { CTL_KERN, KERN_KDEBUG, KERN_KDSET_TYPEFILTER };
  size_t needed = KDBG_TYPEFILTER_BITMAP_SIZE;
  if(sysctl(mib, 3, type_filter_bitmap, &needed, NULL, 0)) {
    quit("trace facility failure, KERN_KDSET_TYPEFILTER\n");
  }
}

void set_pidcheck(int pid, int on_off) {
  kd_regtype kr;

  kr.type = KDBG_TYPENONE;
  kr.value1 = pid;
  kr.value2 = on_off;
  needed = sizeof(kd_regtype);
  mib[0] = CTL_KERN;
  mib[1] = KERN_KDEBUG;
  mib[2] = KERN_KDPIDTR;
  mib[3] = 0;
  mib[4] = 0;
  mib[5] = 0;

  if (sysctl(mib, 3, &kr, &needed, NULL, 0) < 0) {
    if (on_off == 1) {
      fprintf(stderr, "pid %d does not exist\n", pid);
    }
  } else {
    one_good_pid++;
  }
}

/* 
 * on_off == 0 turns off pid exclusion
 * on_off == 1 turns on pid exclusion
 */
void set_pidexclude(int pid, int on_off) {
  kd_regtype kr;

  one_good_pid++;

  kr.type = KDBG_TYPENONE;
  kr.value1 = pid;
  kr.value2 = on_off;
  needed = sizeof(kd_regtype);
  mib[0] = CTL_KERN;
  mib[1] = KERN_KDEBUG;
  mib[2] = KERN_KDPIDEX;
  mib[3] = 0;
  mib[4] = 0;
  mib[5] = 0;

  if (sysctl(mib, 3, &kr, &needed, NULL, 0) < 0) {
    if (on_off == 1) {
      fprintf(stderr, "pid %d does not exist\n", pid);
    }
  }
}

void get_bufinfo(kbufinfo_t *val) {
  needed = sizeof (*val);
  mib[0] = CTL_KERN;
  mib[1] = KERN_KDEBUG;
  mib[2] = KERN_KDGETBUF;   
  mib[3] = 0;
  mib[4] = 0;
  mib[5] = 0;   /* no flags */

  if (sysctl(mib, 3, val, &needed, 0, 0) < 0) {
    quit("trace facility failure, KERN_KDGETBUF\n");
  }
}

void set_remove()  {
  errno = 0;

  mib[0] = CTL_KERN;
  mib[1] = KERN_KDEBUG;
  mib[2] = KERN_KDREMOVE;   /* protocol */
  mib[3] = 0;
  mib[4] = 0;
  mib[5] = 0;   /* no flags */

  if (sysctl(mib, 3, NULL, &needed, NULL, 0) < 0) {
    set_remove_flag = 0;

    if (errno == EBUSY) {
      quit("the trace facility is currently in use...\n          fs_usage, sc_usage, and latency use this feature.\n\n");
    } else {
      quit("trace facility failure, KERN_KDREMOVE\n");
    }
  }
}

void set_init() {
  kd_regtype kr;

  kr.type = KDBG_RANGETYPE;
  kr.value1 = 0;
  kr.value2 = -1;
  needed = sizeof(kd_regtype);

  mib[0] = CTL_KERN;
  mib[1] = KERN_KDEBUG;
  mib[2] = KERN_KDSETREG;   
  mib[3] = 0;
  mib[4] = 0;
  mib[5] = 0;   /* no flags */

  if (sysctl(mib, 3, &kr, &needed, NULL, 0) < 0) {
    quit("trace facility failure, KERN_KDSETREG\n");
  }

  mib[0] = CTL_KERN;
  mib[1] = KERN_KDEBUG;
  mib[2] = KERN_KDSETUP;    
  mib[3] = 0;
  mib[4] = 0;
  mib[5] = 0;   /* no flags */

  if (sysctl(mib, 3, NULL, &needed, NULL, 0) < 0) {
    quit("trace facility failure, KERN_KDSETUP\n");
  }
}


void sample_sc() {
  get_bufinfo(&bufinfo);

  if (need_new_map) {
    read_command_map();
    need_new_map = 0;
  }
  size_t needed = bufinfo.nkdbufs * sizeof(kd_buf);

  mib[0] = CTL_KERN;
  mib[1] = KERN_KDEBUG;
  mib[2] = KERN_KDREADTR;
  mib[3] = 0;
  mib[4] = 0;
  mib[5] = 0;   /* no flags */

  if (sysctl(mib, 3, my_buffer, &needed, NULL, 0) < 0) {
    quit("trace facility failure, KERN_KDREADTR\n");
  }
  int count = needed;

  if (count > (num_events / 8)) {
    if (usleep_ms > USLEEP_BEHIND) {
      usleep_ms = USLEEP_BEHIND;
    } else if (usleep_ms > USLEEP_MIN) {
      usleep_ms /= 2;
    }

  } else if (count < (num_events / 16)) {
    if (usleep_ms < USLEEP_MAX) {
      usleep_ms *= 2;
    }
  }

  if (bufinfo.flags & KDBG_WRAPPED) {
    fprintf(stderr, "fs_usage: buffer overrun, events generated too quickly: %d\n", count);

    delete_all_events();

    need_new_map = 1;

    set_enable(0);
    set_enable(1);
  }
  kd_buf *kd = (kd_buf *)my_buffer;

  for (int i = 0; i < count; i++) {
    uint32_t debugid;
    uintptr_t thread;
    int type;
    int index;
    uintptr_t *sargptr;
    th_info_t ti;


    thread  = kd[i].arg5;
    debugid = kd[i].debugid;
    type    = kd[i].debugid & DBG_FUNC_MASK;

    switch (type) {
    case TRACE_DATA_NEWTHREAD:
      if (kd[i].arg1) {
        if ((ti = add_event(thread, TRACE_DATA_NEWTHREAD)) == NULL) {
          continue;
        }
        ti->child_thread = kd[i].arg1;
        ti->pid = kd[i].arg2;
        /* TODO(peck): Removeme */
        /* printf("newthread PID %d (thread = %d, child_thread = %d)\n", (int)ti->pid, (int)thread, ti->child_thread); */
      }
      continue;

    case TRACE_STRING_NEWTHREAD:
      if ((ti = find_event(thread, TRACE_DATA_NEWTHREAD)) == (struct th_info *)0) {
        continue;
      }

      create_map_entry(ti->child_thread, ti->pid, (char *)&kd[i].arg1);

      delete_event(ti);
      continue;
  
    case TRACE_DATA_EXEC:
      if ((ti = add_event(thread, TRACE_DATA_EXEC)) == NULL) {
        continue;
      }

      ti->pid = kd[i].arg1;
      continue;

    case TRACE_STRING_EXEC:
      if ((ti = find_event(thread, BSC_execve))) {
        if (ti->lookups[0].pathname[0]) {
          exit_event("execve", thread, BSC_execve, 0, 0, 0, 0, FMT_DEFAULT);
        }
      } else if ((ti = find_event(thread, BSC_posix_spawn))) {
        if (ti->lookups[0].pathname[0]) {
          exit_event("posix_spawn", thread, BSC_posix_spawn, 0, 0, 0, 0, FMT_DEFAULT);
        }
      }
      if ((ti = find_event(thread, TRACE_DATA_EXEC)) == (struct th_info *)0) {
        continue;
      }

      create_map_entry(thread, ti->pid, (char *)&kd[i].arg1);

      delete_event(ti);
      continue;

    case BSC_thread_terminate:
      delete_map_entry(thread);
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

    case HFS_modify_block_end:
     if ((ti = find_event(thread, 0))) {
       if (ti->nameptr)
         add_meta_name(kd[i].arg2, reinterpret_cast<const char *>(ti->nameptr));
      }
     continue;

    case VFS_ALIAS_VP:
      add_vnode_name(kd[i].arg2, find_vnode_name(kd[i].arg1));
      continue;

    case VFS_LOOKUP:
      if ((ti = find_event(thread, 0)) == (struct th_info *)0) {
        continue;
      }

      if (debugid & DBG_FUNC_START) {

        if (ti->in_hfs_update) {
          ti->pn_work_index = (MAX_PATHNAMES - 1);
        } else {
          if (ti->pn_scall_index < MAX_SCALL_PATHNAMES) {
            ti->pn_work_index = ti->pn_scall_index;
          } else {
            continue;
          }
        }
        sargptr = &ti->lookups[ti->pn_work_index].pathname[0];

        ti->vnodeid = kd[i].arg1;

        *sargptr++ = kd[i].arg2;
        *sargptr++ = kd[i].arg3;
        *sargptr++ = kd[i].arg4;
        /*
         * NULL terminate the 'string'
         */
        *sargptr = 0;

        ti->pathptr = sargptr;
      } else {
        sargptr = ti->pathptr;

        /*
         * We don't want to overrun our pathname buffer if the
         * kernel sends us more VFS_LOOKUP entries than we can
         * handle and we only handle 2 pathname lookups for
         * a given system call
         */
        if (sargptr == 0) {
          continue;
        }

        if ((uintptr_t)sargptr < (uintptr_t)&ti->lookups[ti->pn_work_index].pathname[NUMPARMS]) {

          *sargptr++ = kd[i].arg1;
          *sargptr++ = kd[i].arg2;
          *sargptr++ = kd[i].arg3;
          *sargptr++ = kd[i].arg4;
          /*
           * NULL terminate the 'string'
           */
          *sargptr = 0;
        }
      }
      if (debugid & DBG_FUNC_END) {

        ti->nameptr = add_vnode_name(
            ti->vnodeid,
            reinterpret_cast<const char *>(&ti->lookups[ti->pn_work_index].pathname[0]));

        if (ti->pn_work_index == ti->pn_scall_index) {

          ti->pn_scall_index++;

          if (ti->pn_scall_index < MAX_SCALL_PATHNAMES) {
            ti->pathptr = &ti->lookups[ti->pn_scall_index].pathname[0];
          } else {
            ti->pathptr = 0;
          }
        }
      } else {
        ti->pathptr = sargptr;
      }

      continue;
    }

    if (debugid & DBG_FUNC_START) {
      const char *p;

      if ((type & CLASS_MASK) == FILEMGR_BASE) {
        index = filemgr_index(type);

        if (index >= MAX_FILEMGR) {
          continue;
        }

        if ((p = filemgr_calls[index].fm_name) == NULL) {
          continue;
        }
      } else {
        p = NULL;
      }

      enter_event(thread, type, &kd[i], p);
      continue;
    }

    switch (type) {
    case Throttled:
       exit_event("  THROTTLED", thread, type, 0, 0, 0, 0, FMT_DEFAULT);
       continue;

    case HFS_update:
       exit_event("  HFS_update", thread, type, kd[i].arg1, kd[i].arg2, 0, 0, FMT_HFS_update);
       continue;

    case SPEC_unmap_info:
     if (check_filter_mode(NULL, SPEC_unmap_info, 0, 0, "SPEC_unmap_info"))
       format_print(NULL, "  TrimExtent", thread, type, kd[i].arg1, kd[i].arg2, kd[i].arg3, 0, FMT_UNMAP_INFO, 0, "");
     continue;

    case SPEC_ioctl:
     if (kd[i].arg2 == DKIOCSYNCHRONIZECACHE) {
       exit_event("IOCTL", thread, type, kd[i].arg1, kd[i].arg2, 0, 0, FMT_IOCTL_SYNCCACHE);
     } else if (kd[i].arg2 == DKIOCUNMAP) {
       exit_event("IOCTL", thread, type, kd[i].arg1, kd[i].arg2, 0, 0, FMT_IOCTL_UNMAP);
     } else if (kd[i].arg2 == DKIOCSYNCHRONIZE && (debugid & DBG_FUNC_ALL) == DBG_FUNC_NONE) {
       exit_event("IOCTL", thread, type, kd[i].arg1, kd[i].arg2, kd[i].arg3, 0, FMT_IOCTL_SYNC);
     } else {
       if ((ti = find_event(thread, type))) {
         delete_event(ti);
       }
     }
     continue;

    case MACH_pageout:
    case MACH_vmfault:
      /* TODO(peck): what about deleting all of the events? */
      if ((ti = find_event(thread, type))) {
        delete_event(ti);
      }
      continue;

    case MSC_map_fd:
      exit_event("map_fd", thread, type, kd[i].arg1, kd[i].arg2, 0, 0, FMT_FD);
      continue;
          
    case BSC_mmap_extended:
    case BSC_mmap_extended2:
    case BSC_msync_extended:
    case BSC_pread_extended:
    case BSC_pwrite_extended:
      extend_syscall(thread, type, &kd[i]);
      continue;
    }

    if ((type & CSC_MASK) == BSC_BASE) {
      if ((index = BSC_INDEX(type)) >= bsd_syscalls.size()) {
        continue;
      }

      if (bsd_syscalls[index].sc_name) {
        exit_event(bsd_syscalls[index].sc_name, thread, type, kd[i].arg1, kd[i].arg2, kd[i].arg3, kd[i].arg4,
             bsd_syscalls[index].sc_format);

        if (type == BSC_exit) {
          delete_map_entry(thread);
        }
      }
    } else if ((type & CLASS_MASK) == FILEMGR_BASE) {
    
      if ((index = filemgr_index(type)) >= MAX_FILEMGR) {
        continue;
      }

      if (filemgr_calls[index].fm_name) {
        exit_event(filemgr_calls[index].fm_name, thread, type, kd[i].arg1, kd[i].arg2, kd[i].arg3, kd[i].arg4,
             FMT_DEFAULT);
      }
    }
  }
  fflush(0);
}


void enter_event_now(uintptr_t thread, int type, kd_buf *kd, const char *name) {
  th_info_t ti;
  char buf[MAXWIDTH];
  buf[0] = 0;

  if ((ti = add_event(thread, type)) == NULL) {
    return;
  }

  ti->arg1   = kd->arg1;
  ti->arg2   = kd->arg2;
  ti->arg3   = kd->arg3;
  ti->arg4   = kd->arg4;

  switch (type) {
  case HFS_update:
    ti->in_hfs_update = 1;
    break;
  }

  if ((type & CLASS_MASK) == FILEMGR_BASE) {

    filemgr_in_progress++;
    ti->in_filemgr = 1;

    int tsclen = strlen(buf);  /* TODO(peck): I think this is empty */

    /*
     * Print timestamp column
     */
    printf("%s", buf);

    threadmap_t tme = find_map_entry(thread);
    if (tme) {
      sprintf(buf, "  %-25.25s ", name);
      int nmclen = strlen(buf);
      printf("%s", buf);

      sprintf(buf, "(%d, 0x%lx, 0x%lx, 0x%lx)", (short)kd->arg1, kd->arg2, kd->arg3, kd->arg4);
      int argsclen = strlen(buf);

      printf("%s", buf);   /* print the kdargs */
      printf("%s.%d\n", tme->tm_command, (int)thread); 
    } else {
      printf("  %-24.24s (%5d, %#lx, 0x%lx, 0x%lx)\n",         name, (short)kd->arg1, kd->arg2, kd->arg3, kd->arg4);
    }
  }
}


void enter_event(uintptr_t thread, int type, kd_buf *kd, const char *name) {
  switch (type) {

  case MSC_map_fd:
  case SPEC_ioctl:
  case Throttled:
  case HFS_update:
    enter_event_now(thread, type, kd, name);
    return;

  }
  if ((type & CSC_MASK) == BSC_BASE) {
    int index = BSC_INDEX(type);
    if (index >= bsd_syscalls.size()) {
      return;
    }

    if (bsd_syscalls[index].sc_name) {
      enter_event_now(thread, type, kd, name);
    }
    return;
  }
  if ((type & CLASS_MASK) == FILEMGR_BASE) {
    int index = index = filemgr_index(type);
    if (index >= MAX_FILEMGR) {
      return;
    }
         
    if (filemgr_calls[index].fm_name) {
      enter_event_now(thread, type, kd, name);
    }
    return;
  }
}

/*
 * Handle system call extended trace data.
 * pread and pwrite:
 *     Wipe out the kd args that were collected upon syscall_entry
 *     because it is the extended info that we really want, and it
 *     is all we really need.
*/

void extend_syscall(uintptr_t thread, int type, kd_buf *kd) {
  th_info_t ti;

  switch (type) {
  case BSC_mmap_extended:
    if ((ti = find_event(thread, BSC_mmap)) == (struct th_info *)0)
      return;
    ti->arg8   = ti->arg3;  /* save protection */
    ti->arg1   = kd->arg1;  /* the fd */
    ti->arg3   = kd->arg2;  /* bottom half address */
    ti->arg5   = kd->arg3;  /* bottom half size */
    break;
  case BSC_mmap_extended2:
    if ((ti = find_event(thread, BSC_mmap)) == (struct th_info *)0)
      return;
    ti->arg2   = kd->arg1;  /* top half address */
    ti->arg4   = kd->arg2;  /* top half size */
    ti->arg6   = kd->arg3;  /* top half file offset */
    ti->arg7   = kd->arg4;  /* bottom half file offset */
    break;
  case BSC_msync_extended:
    if ((ti = find_event(thread, BSC_msync)) == (struct th_info *)0) {
      if ((ti = find_event(thread, BSC_msync_nocancel)) == (struct th_info *)0)
        return;
    }
    ti->arg4   = kd->arg1;  /* top half address */
    ti->arg5   = kd->arg2;  /* top half size */
    break;
  case BSC_pread_extended:
    if ((ti = find_event(thread, BSC_pread)) == (struct th_info *)0) {
      if ((ti = find_event(thread, BSC_pread_nocancel)) == (struct th_info *)0)
        return;
    }
    ti->arg1   = kd->arg1;  /* the fd */
    ti->arg2   = kd->arg2;  /* nbytes */
    ti->arg3   = kd->arg3;  /* top half offset */
    ti->arg4   = kd->arg4;  /* bottom half offset */
    break;
  case BSC_pwrite_extended:
    if ((ti = find_event(thread, BSC_pwrite)) == (struct th_info *)0) {
      if ((ti = find_event(thread, BSC_pwrite_nocancel)) == (struct th_info *)0)
        return;
    }
    ti->arg1   = kd->arg1;  /* the fd */
    ti->arg2   = kd->arg2;  /* nbytes */
    ti->arg3   = kd->arg3;  /* top half offset */
    ti->arg4   = kd->arg4;  /* bottom half offset */
    break;
  default:
    return;
  }
}


void exit_event(
    const char *sc_name,
    uintptr_t thread,
    int type,
    uintptr_t arg1,
    uintptr_t arg2,
    uintptr_t arg3,
    uintptr_t arg4,
    int format) {
  th_info_t ti;
      
  if ((ti = find_event(thread, type)) == (struct th_info *)0) {
    return;
  }

  ti->nameptr = 0;

  if (check_filter_mode(ti, type, arg1, arg2, sc_name)) {
    format_print(ti, sc_name, thread, type, arg1, arg2, arg3, arg4, format, ti->waited, (char *)&ti->lookups[0].pathname[0]);
  }

  switch (type) {

  case HFS_update:
    ti->in_hfs_update = 0;
    break;
  }
  if ((type & CLASS_MASK) == FILEMGR_BASE) {
    ti->in_filemgr = 0;

    if (filemgr_in_progress > 0) {
      filemgr_in_progress--;
    }
  }
  delete_event(ti);
}


void get_mode_nibble(char * buf, int smode, int special, char x_on, char x_off) {
  if (smode & 04) {
    buf[0] = 'r';
  }
  if (smode & 02) {
    buf[1] = 'w';
  }
  if (smode & 01) {
    if (special) {
      buf[2] = x_on;
    } else {
      buf[2] = 'x';
    }
  } else {
    if (special) {
      buf[2] = x_off;
    }
  }
}


void get_mode_string(int mode, char *buf) {
  memset(buf, '-', 9);
  buf[9] = '\0';

  get_mode_nibble(&buf[6], mode, (mode & 01000), 't', 'T');
  get_mode_nibble(&buf[3], (mode>>3), (mode & 02000), 's', 'S');
  get_mode_nibble(&buf[0], (mode>>6), (mode & 04000), 's', 'S');
}


int clip_64bit(const char *s, uint64_t value) {
  int clen = 0;

  if ((value & 0xff00000000000000LL)) {
    clen = printf("%s0x%16.16qx", s, value);
  } else if ((value & 0x00ff000000000000LL)) {
    clen = printf("%s0x%14.14qx  ", s, value);
  } else if ((value & 0x0000ff0000000000LL)) {
    clen = printf("%s0x%12.12qx    ", s, value);
  } else if ((value & 0x000000ff00000000LL)) {
    clen = printf("%s0x%10.10qx      ", s, value);
  } else {
    clen = printf("%s0x%8.8qx        ", s, value);
  }
  
  return clen;
}


void format_print(
    struct th_info *ti,
    const char *sc_name,
    uintptr_t thread,
    int type,
    uintptr_t arg1,
    uintptr_t arg2,
    uintptr_t arg3,
    uintptr_t arg4,
    int format,
    int waited,
    const char *pathname) {
  int nopadding = 0;
  const char *command_name;
  int in_filemgr = 0;
  int len = 0;
  int tlen = 0;
  int klass;
  uint64_t user_addr;
  uint64_t user_size;
  char *framework_name;
  char *framework_type;
  const char *p1;
  const char *p2;
  char buf[MAXWIDTH];

  static char timestamp[32];
  static int last_timestamp = -1;
  static int timestamp_len = 0;

  command_name = "";

  // <rdar://problem/19852325> Filter out WindowServer/xcpm iocts in fs_usage
  if (format == FMT_IOCTL && ti->arg2 == 0xc030581d) {
    return;
  }

  klass = type >> 24;

  threadmap_t tme;

  if ((tme = find_map_entry(thread))) {
    command_name = tme->tm_command;
  }
  tlen = timestamp_len;
  nopadding = 0;

  timestamp[tlen] = '\0';

  if (filemgr_in_progress) {
    if (klass != FILEMGR_CLASS) {
      if (find_event(thread, -1)) {
        in_filemgr = 1;
      }
    }
  }

  if (klass == FILEMGR_CLASS) {
    printf("%s  %-20.20s", timestamp, sc_name);
  } else if (in_filemgr) {
    printf("%s    %-15.15s", timestamp, sc_name);
  } else {
    printf("%s  %-17.17s", timestamp, sc_name);
  }
       

  framework_name = NULL;

  if (1) {  /* TODO(peck): Clean me up */
    off_t offset_reassembled = 0LL;
    
    switch (format) {
    case FMT_AT:
    case FMT_RENAMEAT:
    case FMT_DEFAULT:
      /*
       * pathname based system calls or 
       * calls with no fd or pathname (i.e.  sync)
       */
      if (arg1)
        printf("      [%3lu]       ", arg1);
      else
        printf("                  ");
      break;

    case FMT_FD:
      /*
       * fd based system call... no I/O
       */
      if (arg1)
        printf(" F=%-3d[%3lu]", ti->arg1, arg1);
      else
        printf(" F=%-3d", ti->arg1);
      break;

    case FMT_FD_2:
      /*
       * accept, dup, dup2
       */
      if (arg1)
        printf(" F=%-3d[%3lu]", ti->arg1, arg1);
      else
        printf(" F=%-3d  F=%-3lu", ti->arg1, arg2);
      break;

    case FMT_FD_IO:
      /*
       * system calls with fd's that return an I/O completion count
       */
      if (arg1)
        printf(" F=%-3d[%3lu]", ti->arg1, arg1);
      else
        printf(" F=%-3d  B=0x%-6lx", ti->arg1, arg2);
      break;

    case FMT_HFS_update:
    {
      char sbuf[7];
      int sflag = (int)arg2;

      memset(sbuf, '_', 6);
      sbuf[6] = '\0';

      
      if (sflag & 0x10)
        sbuf[0] = 'F';
      if (sflag & 0x08)
        sbuf[1] = 'M';
      if (sflag & 0x20)
        sbuf[2] = 'D';
      if (sflag & 0x04)
        sbuf[3] = 'c';
      if (sflag & 0x01)
        sbuf[4] = 'a';
      if (sflag & 0x02)
        sbuf[5] = 'm';

      printf("            (%s) ", sbuf);

      pathname = find_vnode_name(arg1);
      nopadding = 1;

      break;
    }

    case FMT_MSYNC:
    {
      /*
       * msync
       */
      int mlen = 0;

      buf[0] = '\0';

      if (ti->arg3 & MS_ASYNC) {
        mlen += sprintf(&buf[mlen], "MS_ASYNC | ");
      } else {
        mlen += sprintf(&buf[mlen], "MS_SYNC | ");
      }

      if (ti->arg3 & MS_INVALIDATE) {
        mlen += sprintf(&buf[mlen], "MS_INVALIDATE | ");
      } if (ti->arg3 & MS_KILLPAGES) {
        mlen += sprintf(&buf[mlen], "MS_KILLPAGES | ");
      } if (ti->arg3 & MS_DEACTIVATE) {
        mlen += sprintf(&buf[mlen], "MS_DEACTIVATE | ");
      }

      if (ti->arg3 & ~(MS_ASYNC | MS_SYNC | MS_INVALIDATE | MS_KILLPAGES | MS_DEACTIVATE)) {
        mlen += sprintf(&buf[mlen], "UNKNOWN | ");
      }
      
      if (mlen) {
        buf[mlen - 3] = '\0';
      }

      if (arg1) {
        printf("      [%3lu]", arg1);
      }

      user_addr = (((off_t)(unsigned int)(ti->arg4)) << 32) | (unsigned int)(ti->arg1);
      clip_64bit(" A=", user_addr);

      user_size = (((off_t)(unsigned int)(ti->arg5)) << 32) | (unsigned int)(ti->arg2);

      printf("  B=0x%-16qx  <%s>", user_size, buf);

      break;
    }

    case FMT_FLOCK:
    {
      /*
       * flock
       */
      int mlen = 0;

      buf[0] = '\0';

      if (ti->arg2 & LOCK_SH) {
        mlen += sprintf(&buf[mlen], "LOCK_SH | ");
      }
      if (ti->arg2 & LOCK_EX) {
        mlen += sprintf(&buf[mlen], "LOCK_EX | ");
      }
      if (ti->arg2 & LOCK_NB) {
        mlen += sprintf(&buf[mlen], "LOCK_NB | ");
      }
      if (ti->arg2 & LOCK_UN) {
        mlen += sprintf(&buf[mlen], "LOCK_UN | ");
      }

      if (ti->arg2 & ~(LOCK_SH | LOCK_EX | LOCK_NB | LOCK_UN)) {
        mlen += sprintf(&buf[mlen], "UNKNOWN | ");
      }
      
      if (mlen) {
        buf[mlen - 3] = '\0';
      }

      if (arg1) {
        printf(" F=%-3d[%3lu]  <%s>", ti->arg1, arg1, buf);
      } else {
        printf(" F=%-3d  <%s>", ti->arg1, buf);
      }

      break;
    }

    case FMT_FCNTL:
    {
      /*
       * fcntl
       */
      const char *p = NULL;
      int fd = -1;

      if (arg1) {
        printf(" F=%-3d[%3lu]", ti->arg1, arg1);
      } else {
        printf(" F=%-3d", ti->arg1);
      }

      switch (ti->arg2) {
      case F_DUPFD:
        p = "DUPFD";
        break;

      case F_GETFD:
        p = "GETFD";
        break;

      case F_SETFD:
        p = "SETFD";
        break;

      case F_GETFL:
        p = "GETFL";
        break;

      case F_SETFL:
        p = "SETFL";
        break;

      case F_GETOWN:
        p = "GETOWN";
        break;

      case F_SETOWN:
        p = "SETOWN";
        break;

      case F_GETLK:
        p = "GETLK";
        break;
        
      case F_SETLK:
        p = "SETLK";
        break;

      case F_SETLKW:
        p = "SETLKW";
        break;
        
      case F_PREALLOCATE:
        p = "PREALLOCATE";
        break;

      case F_SETSIZE:
        p = "SETSIZE";
        break;

      case F_RDADVISE:
        p = "RDADVISE";
        break;

      case F_GETPATH:
        p = "GETPATH";
        break;

      case F_FULLFSYNC:
        p = "FULLFSYNC";
        break;

      case F_PATHPKG_CHECK:
        p = "PATHPKG_CHECK";
        break;

      case F_OPENFROM:
        p = "OPENFROM";
        
        if (arg1 == 0) {
          fd = arg2;
        }
        break;

      case F_UNLINKFROM:
        p = "UNLINKFROM";
        break;

      case F_CHECK_OPENEVT:
        p = "CHECK_OPENEVT";
        break;

      case F_NOCACHE:
        if (ti->arg3) {
          p = "CACHING OFF";
        } else {
          p = "CACHING ON";
        }
        break;

      case F_GLOBAL_NOCACHE:
        if (ti->arg3) {
          p = "CACHING OFF (GLOBAL)";
        } else {
          p = "CACHING ON (GLOBAL)";
        }
        break;

      }
      if (p) {
        if (fd == -1) {
          printf(" <%s>", p);
        } else {
          printf(" <%s> F=%d", p, fd);
        }
      } else {
        printf(" <CMD=%d>", ti->arg2);
      }

      break;
    }

    case FMT_IOCTL:
    {
      /*
       * ioctl
       */
      if (arg1) {
        printf(" F=%-3d[%3lu]", ti->arg1, arg1);
      } else {
        printf(" F=%-3d", ti->arg1);
      }

      printf(" <CMD=0x%x>", ti->arg2);

      break;
    }

    case FMT_SELECT:
    {
      /*
       * select
       */
      if (arg1) {
        printf("      [%3lu]", arg1);
      } else {
        printf("        S=%-3lu", arg2);
      }

      break;
    }

    case FMT_LSEEK:
    case FMT_PREAD:
      /*
       * pread, pwrite, lseek
       */
      printf(" F=%-3d", ti->arg1);

      if (arg1) {
        printf("[%3lu]  ", arg1);
      } else {
        if (format == FMT_PREAD) {
          printf("  B=0x%-8lx ", arg2);
        } else {
          printf("  ");
        }
      }
      if (format == FMT_PREAD) {
        offset_reassembled = (((off_t)(unsigned int)(ti->arg3)) << 32) | (unsigned int)(ti->arg4);
      } else {
#ifdef __ppc__
        offset_reassembled = (((off_t)(unsigned int)(arg2)) << 32) | (unsigned int)(arg3);
#else
        offset_reassembled = (((off_t)(unsigned int)(arg3)) << 32) | (unsigned int)(arg2);
#endif
      }
      clip_64bit("O=", offset_reassembled);

      if (format == FMT_LSEEK) {
        const char *mode;

        if (ti->arg4 == SEEK_SET) {
          mode = "SEEK_SET";
        } else if (ti->arg4 == SEEK_CUR) {
          mode = "SEEK_CUR";
        } else if (ti->arg4 == SEEK_END) {
          mode = "SEEK_END";
        } else {
          mode = "UNKNOWN";
        }
        
        printf(" <%s>", mode);
      }
      break;

    case FMT_MMAP:
      /*
       * mmap
       */
      printf(" F=%-3d  ", ti->arg1);

      if (arg1) {
        printf("[%3lu]  ", arg1);
      } else {
        user_addr = (((off_t)(unsigned int)(ti->arg2)) << 32) | (unsigned int)(ti->arg3);

        clip_64bit("A=", user_addr);
              offset_reassembled = (((off_t)(unsigned int)(ti->arg6)) << 32) | (unsigned int)(ti->arg7);

        clip_64bit("O=", offset_reassembled);
              user_size = (((off_t)(unsigned int)(ti->arg4)) << 32) | (unsigned int)(ti->arg5);

        printf("B=0x%-16qx", user_size);
        
        printf(" <");

        if (ti->arg8 & PROT_READ) {
          printf("READ");
        }

        if (ti->arg8 & PROT_WRITE) {
          printf("|WRITE");
        }

        if (ti->arg8 & PROT_EXEC) {
          printf("|EXEC");
        }

        printf(">");
      }
      break;

    case FMT_TRUNC:
    case FMT_FTRUNC:
      /*
       * ftruncate, truncate
       */
      if (format == FMT_FTRUNC) {
        printf(" F=%-3d", ti->arg1);
      } else {
        printf("      ");
      }

      if (arg1) {
        printf("[%3lu]", arg1);
      }

#ifdef __ppc__
      offset_reassembled = (((off_t)(unsigned int)(ti->arg2)) << 32) | (unsigned int)(ti->arg3);
#else
      offset_reassembled = (((off_t)(unsigned int)(ti->arg3)) << 32) | (unsigned int)(ti->arg2);
#endif
      clip_64bit("  O=", offset_reassembled);

      nopadding = 1;
      break;

    case FMT_FCHFLAGS:
    case FMT_CHFLAGS:
    {
      /*
       * fchflags, chflags
       */
      int mlen = 0;

      if (format == FMT_FCHFLAGS) {
        if (arg1) {
          printf(" F=%-3d[%3lu]", ti->arg1, arg1);
        } else {
          printf(" F=%-3d", ti->arg1);
        }
      } else {
        if (arg1) {
          printf(" [%3lu] ", arg1);
        }
      }
      buf[mlen++] = ' ';
      buf[mlen++] = '<';

      if (ti->arg2 & UF_NODUMP) {
        mlen += sprintf(&buf[mlen], "UF_NODUMP | ");
      }
      if (ti->arg2 & UF_IMMUTABLE) {
        mlen += sprintf(&buf[mlen], "UF_IMMUTABLE | ");
      }
      if (ti->arg2 & UF_APPEND) {
        mlen += sprintf(&buf[mlen], "UF_APPEND | ");
      }
      if (ti->arg2 & UF_OPAQUE) {
        mlen += sprintf(&buf[mlen], "UF_OPAQUE | ");
      }
      if (ti->arg2 & SF_ARCHIVED) {
        mlen += sprintf(&buf[mlen], "SF_ARCHIVED | ");
      }
      if (ti->arg2 & SF_IMMUTABLE) {
        mlen += sprintf(&buf[mlen], "SF_IMMUTABLE | ");
      }
      if (ti->arg2 & SF_APPEND) {
        mlen += sprintf(&buf[mlen], "SF_APPEND | ");
      }
      
      if (ti->arg2 == 0) {
        mlen += sprintf(&buf[mlen], "CLEAR_ALL_FLAGS | ");
      } else if (ti->arg2 & ~(UF_NODUMP | UF_IMMUTABLE | UF_APPEND | SF_ARCHIVED | SF_IMMUTABLE | SF_APPEND)) {
        mlen += sprintf(&buf[mlen], "UNKNOWN | ");
      }

      if (mlen >= 3) {
        mlen -= 3;
      }

      buf[mlen++] = '>';
      buf[mlen] = '\0';

      if (mlen < 19) {
        memset(&buf[mlen], ' ', 19 - mlen);
        mlen = 19;
      }
      printf("%s", buf);

      nopadding = 1;
      break;
    }

    case FMT_UMASK:
    case FMT_FCHMOD:
    case FMT_FCHMOD_EXT:
    case FMT_CHMOD:
    case FMT_CHMOD_EXT:
    case FMT_CHMODAT:
    {
      /*
       * fchmod, fchmod_extended, chmod, chmod_extended
       */
      int mode;

      if (format == FMT_FCHMOD || format == FMT_FCHMOD_EXT) {
        if (arg1) {
          printf(" F=%-3d[%3lu] ", ti->arg1, arg1);
        } else {
          printf(" F=%-3d ", ti->arg1);
        }
      } else {
        if (arg1) {
          printf(" [%3lu] ", arg1);
        } else {
          printf(" ");
        }
      }
      if (format == FMT_UMASK) {
        mode = ti->arg1;
      } else if (format == FMT_FCHMOD || format == FMT_CHMOD || format == FMT_CHMODAT) {
        mode = ti->arg2;
      } else {
        mode = ti->arg4;
      }

      get_mode_string(mode, &buf[0]);

      if (arg1 == 0) {
        printf("<%s>      ", buf);
      } else {
        printf("<%s>", buf);
      }
      break;
    }

    case FMT_ACCESS:
    {
      /*
       * access
       */
      char mode[5];
      
      memset(mode, '_', 4);
      mode[4] = '\0';

      if (ti->arg2 & R_OK) {
        mode[0] = 'R';
      }
      if (ti->arg2 & W_OK) {
        mode[1] = 'W';
      }
      if (ti->arg2 & X_OK) {
        mode[2] = 'X';
      }
      if (ti->arg2 == F_OK) {
        mode[3] = 'F';
      }

      if (arg1) {
        printf("      [%3lu] (%s)   ", arg1, mode);
      } else {
        printf("            (%s)   ", mode);
      }

      nopadding = 1;
      break;
    }

    case FMT_MOUNT:
    {
      if (arg1) {
        printf("      [%3lu] <FLGS=0x%x> ", arg1, ti->arg3);
      } else {
        printf("     <FLGS=0x%x> ", ti->arg3);
      }

      nopadding = 1;
      break;
    }

    case FMT_UNMOUNT:
    {
      const char *mountflag;

      if (ti->arg2 & MNT_FORCE) {
        mountflag = "<FORCE>";
      } else {
        mountflag = "";
      }

      if (arg1) {
        printf("      [%3lu] %s  ", arg1, mountflag);
      } else {
        printf("     %s         ", mountflag);
      }

      nopadding = 1;
      break;
    }

    case FMT_OPENAT:
    case FMT_OPEN:
    {
      /*
       * open
       */
      char mode[7];
      
      memset(mode, '_', 6);
      mode[6] = '\0';

      if (ti->arg2 & O_RDWR) {
        mode[0] = 'R';
        mode[1] = 'W';
      } else if (ti->arg2 & O_WRONLY) {
        mode[1] = 'W';
      } else {
        mode[0] = 'R';
      }

      if (ti->arg2 & O_CREAT) {
        mode[2] = 'C';
      }
        
      if (ti->arg2 & O_APPEND) {
        mode[3] = 'A';
      }
        
      if (ti->arg2 & O_TRUNC) {
        mode[4] = 'T';
      }
        
      if (ti->arg2 & O_EXCL) {
        mode[5] = 'E';
      }

      if (arg1) {
        printf("      [%3lu] (%s) ", arg1, mode);
      } else {
        printf(" F=%-3lu      (%s) ", arg2, mode);
      }

      nopadding = 1;
      break;
    }

    case FMT_SOCKET:
    {
      /*
       * socket
       * 
       */
      const char *domain;
      const char *type;
      
      switch (ti->arg1) {

      case AF_UNIX:
        domain = "AF_UNIX";
        break;

      case AF_INET:
        domain = "AF_INET";
        break;

      case AF_ISO:
        domain = "AF_ISO";
        break;

      case AF_NS:
        domain = "AF_NS";
        break;

      case AF_IMPLINK:
        domain = "AF_IMPLINK";
        break;

      default:
        domain = "UNKNOWN";
        break;
      }

      switch (ti->arg2) {
        
      case SOCK_STREAM:
        type = "SOCK_STREAM";
        break;

      case SOCK_DGRAM:
        type = "SOCK_DGRAM";
        break;

      case SOCK_RAW:
        type = "SOCK_RAW";
        break;

      case SOCK_SEQPACKET:
        type = "SOCK_SEQPACKET";
        break;

      case SOCK_RDM:
        type = "SOCK_RDM";
        break;
        
      default:
        type = "UNKNOWN";
        break;
      }

      if (arg1) {
        printf("      [%3lu] <%s, %s, 0x%x>", arg1, domain, type, ti->arg3);
      } else {
        printf(" F=%-3lu      <%s, %s, 0x%x>", arg2, domain, type, ti->arg3);
      }
      break;
    }

    case FMT_AIO_FSYNC:
    {
      /*
       * aio_fsync    [errno]   AIOCBP   OP
       */
      const char *op;

      if (ti->arg1 == O_SYNC || ti->arg1 == 0) {
        op = "AIO_FSYNC";
      }
#if O_DSYNC
      else if (ti->arg1 == O_DSYNC) {
        op = "AIO_DSYNC";
      }
#endif      
      else {
        op = "UNKNOWN";
      }

      if (arg1) {
        printf("      [%3lu] P=0x%8.8x  <%s>", arg1, ti->arg2, op);
      } else {
        printf("            P=0x%8.8x  <%s>", ti->arg2, op);
      }
      break;
    }

    case FMT_AIO_RETURN:
      /*
       * aio_return   [errno]   AIOCBP   IOSIZE
       */
      if (arg1) {
        printf("      [%3lu] P=0x%8.8x", arg1, ti->arg1);
      } else {
        printf("            P=0x%8.8x  B=0x%-8lx", ti->arg1, arg2);
      }
      break;

    case FMT_AIO_SUSPEND:
      /*
       * aio_suspend    [errno]   NENTS
       */
      if (arg1) {
        printf("      [%3lu] N=%d", arg1, ti->arg2);
      } else {
        printf("            N=%d", ti->arg2);
      }
      break;

    case FMT_AIO_CANCEL:
      /*
       * aio_cancel     [errno]   FD or AIOCBP (if non-null)
       */
      if (ti->arg2) {
        if (arg1) {
          printf("      [%3lu] P=0x%8.8x", arg1, ti->arg2);
        } else {
          printf("            P=0x%8.8x", ti->arg2);
        }
      } else {
        if (arg1) {
          printf(" F=%-3d[%3lu]", ti->arg1, arg1);
        } else {
          printf(" F=%-3d", ti->arg1);
        }
      }
      break;

    case FMT_AIO:
      /*
       * aio_error, aio_read, aio_write [errno]  AIOCBP
       */
      if (arg1) {
        printf("      [%3lu] P=0x%8.8x", arg1, ti->arg1);
      } else {
        printf("            P=0x%8.8x", ti->arg1);
      }
      break;

    case FMT_LIO_LISTIO:
    {
      /*
       * lio_listio   [errno]   NENTS  MODE
       */
      const char *op;

      if (ti->arg1 == LIO_NOWAIT) {
        op = "LIO_NOWAIT";
      } else if (ti->arg1 == LIO_WAIT) {
        op = "LIO_WAIT";
      } else {
        op = "UNKNOWN";
      }

      if (arg1) {
        printf("      [%3lu] N=%d  <%s>", arg1, ti->arg3, op);
      } else {
        printf("            N=%d  <%s>", ti->arg3, op);
      }
      break;
    }
    }
  }

  if (framework_name) {
    len = sprintf(&buf[0], " %s %s ", framework_type, framework_name);
  } else if (*pathname != '\0') {
    switch(format) {
    case FMT_AT:
    case FMT_OPENAT:
    case FMT_CHMODAT:
      len = sprintf(&buf[0], " [%d]/%s ", ti->arg1, pathname);
      break;
    case FMT_RENAMEAT:
      len = sprintf(&buf[0], " [%d]/%s ", ti->arg3, pathname);
      break;
    default:
      len = sprintf(&buf[0], " %s ", pathname);
    }

    if (format == FMT_MOUNT && ti->lookups[1].pathname[0]) {
      int len2;

      memset(&buf[len], ' ', 2);

      len2 = sprintf(&buf[len+2], " %s ", (char *)&ti->lookups[1].pathname[0]);
      len = len + 2 + len2;
    }
  } else {
    len = 0;
  }

  pathname = buf;
  
  if (klass != FILEMGR_CLASS && !nopadding) {
    p1 = "   ";
  } else {
    p1 = "";
  }
         
  if (waited) {
    p2 = " W";
  } else {
    p2 = "  ";
  }

  printf("%s%s %s %s.%d\n", p1, pathname, p2, command_name, (int)thread);
}


void add_meta_name(uint64_t blockno, const char *pathname) {
  meta_info_t mi;

  int hashid = blockno & VN_HASH_MASK;

  for (mi = m_info_hash[hashid]; mi; mi = mi->m_next) {
    if (mi->m_blkno == blockno) {
      break;
    }
  }
  if (mi == NULL) {
    mi = (meta_info_t)malloc(sizeof(struct meta_info));
    
    mi->m_next = m_info_hash[hashid];
    m_info_hash[hashid] = mi;
    mi->m_blkno = blockno;
  }
  mi->m_nameptr = pathname;
}

char *add_vnode_name(uint64_t vn_id, const char *pathname) {
  vnode_info_t vn;

  int hashid = (vn_id >> VN_HASH_SHIFT) & VN_HASH_MASK;

  for (vn = vn_info_hash[hashid]; vn; vn = vn->vn_next) {
    if (vn->vn_id == vn_id) {
      break;
    }
  }
  if (vn == NULL) {
    vn = (vnode_info_t)malloc(sizeof(struct vnode_info));
    
    vn->vn_next = vn_info_hash[hashid];
    vn_info_hash[hashid] = vn;
    vn->vn_id = vn_id;
  }
  strcpy(reinterpret_cast<char *>(vn->vn_pathname), pathname);

  return reinterpret_cast<char *>(&vn->vn_pathname);
}


const char *find_vnode_name(uint64_t vn_id) {
  int hashid = (vn_id >> VN_HASH_SHIFT) & VN_HASH_MASK;

  for (vnode_info_t vn = vn_info_hash[hashid]; vn; vn = vn->vn_next) {
    if (vn->vn_id == vn_id) {
      return reinterpret_cast<char *>(vn->vn_pathname);
    }
  }
  return "";
}


void delete_event(th_info_t ti_to_delete) {
  th_info_t ti;
  th_info_t ti_prev;

  int hashid = ti_to_delete->thread & HASH_MASK;

  if ((ti = th_info_hash[hashid])) {
    if (ti == ti_to_delete) {
      th_info_hash[hashid] = ti->next;
    } else {
      ti_prev = ti;

      for (ti = ti->next; ti; ti = ti->next) {
        if (ti == ti_to_delete) {
          ti_prev->next = ti->next;
          break;
        }
        ti_prev = ti;
      }
    }
    if (ti) {
      ti->next = th_info_freelist;
      th_info_freelist = ti;
    }
  }
}

th_info_t add_event(uintptr_t thread, int type) {
  th_info_t ti;

  if ((ti = th_info_freelist)) {
    th_info_freelist = ti->next;
  } else {
    ti = (th_info_t)malloc(sizeof(struct th_info));
  }

  int hashid = thread & HASH_MASK;

  ti->next = th_info_hash[hashid];
  th_info_hash[hashid] = ti;

  ti->thread = thread;
  ti->type = type;

  ti->waited = 0;
  ti->in_filemgr = 0;
  ti->in_hfs_update = 0;

  ti->pathptr = &ti->lookups[0].pathname[0];
  ti->pn_scall_index = 0;
  ti->pn_work_index = 0;

  for (int i = 0; i < MAX_PATHNAMES; i++) {
    ti->lookups[i].pathname[0] = 0;
  }

  return ti;
}

th_info_t find_event(uintptr_t thread, int type) {
  int hashid = thread & HASH_MASK;

  for (th_info_t ti = th_info_hash[hashid]; ti; ti = ti->next) {
    if (ti->thread == thread) {
      if (type == ti->type) {
        return ti;
      }
      if (ti->in_filemgr) {
        if (type == -1) {
          return ti;
        }
        continue;
      }
      if (type == 0) {
        return ti;
      }
    }
  }
  return ((th_info_t) 0);
}

void delete_all_events() {
  th_info_t ti_next = 0;

  for (int i = 0; i < HASH_SIZE; i++) {
    for (th_info_t ti = th_info_hash[i]; ti; ti = ti_next) {
      ti_next = ti->next;
      ti->next = th_info_freelist;
      th_info_freelist = ti;
    }
    th_info_hash[i] = 0;
  }
}

void read_command_map() {
  kd_threadmap *mapptr = 0;

  delete_all_map_entries();

  int total_threads = bufinfo.nkdthreads;
  size_t size = bufinfo.nkdthreads * sizeof(kd_threadmap);

  if (size) {
    if ((mapptr = reinterpret_cast<kd_threadmap *>(malloc(size)))) {
      int mib[6];

      bzero (mapptr, size);
      /*
       * Now read the threadmap
       */
      mib[0] = CTL_KERN;
      mib[1] = KERN_KDEBUG;
      mib[2] = KERN_KDTHRMAP;
      mib[3] = 0;
      mib[4] = 0;
      mib[5] = 0;   /* no flags */

      if (sysctl(mib, 3, mapptr, &size, NULL, 0) < 0) {
        /*
         * This is not fatal -- just means I cant map command strings
         */
        free(mapptr);
        return;
      }
    }
  }
  for (int i = 0; i < total_threads; i++) {
    create_map_entry(mapptr[i].thread, mapptr[i].valid, &mapptr[i].command[0]);
  }

  free(mapptr);
}


void delete_all_map_entries() {
  threadmap_t tme_next = 0;

  for (int i = 0; i < HASH_SIZE; i++) {
    for (threadmap_t tme = threadmap_hash[i]; tme; tme = tme_next) {
      if (tme->tm_setptr) {
        free(tme->tm_setptr);
      }
      tme_next = tme->tm_next;
      tme->tm_next = threadmap_freelist;
      threadmap_freelist = tme;
    }
    threadmap_hash[i] = 0;
  }
}


void create_map_entry(uintptr_t thread, int pid, char *command) {
  threadmap_t tme;

  if ((tme = threadmap_freelist)) {
    threadmap_freelist = tme->tm_next;
  } else {
    tme = (threadmap_t)malloc(sizeof(struct threadmap));
  }

  tme->tm_thread  = thread;
  tme->tm_setsize = 0;
  tme->tm_setptr  = 0;

  (void)strncpy (tme->tm_command, command, MAXCOMLEN);
  tme->tm_command[MAXCOMLEN] = '\0';

  int hashid = thread & HASH_MASK;

  tme->tm_next = threadmap_hash[hashid];
  threadmap_hash[hashid] = tme;

  if (pid != 0 && pid != 1) {
    if (!strncmp(command, "LaunchCFMA", 10)) {
      (void)get_real_command_name(pid, tme->tm_command, MAXCOMLEN);
    }
  }
}


threadmap_t find_map_entry(uintptr_t thread) {
  int hashid = thread & HASH_MASK;

  for (threadmap_t tme = threadmap_hash[hashid]; tme; tme = tme->tm_next) {
    if (tme->tm_thread == thread) {
      return tme;
    }
  }
  return 0;
}


void delete_map_entry(uintptr_t thread) {
  threadmap_t tme = 0;
  threadmap_t tme_prev;

  int hashid = thread & HASH_MASK;

  if ((tme = threadmap_hash[hashid])) {
    if (tme->tm_thread == thread) {
      threadmap_hash[hashid] = tme->tm_next;
    } else {
      tme_prev = tme;

      for (tme = tme->tm_next; tme; tme = tme->tm_next) {
        if (tme->tm_thread == thread) {
          tme_prev->tm_next = tme->tm_next;
          break;
        }
        tme_prev = tme;
      }
    }
    if (tme) {
      if (tme->tm_setptr) {
        free(tme->tm_setptr);
      }

      tme->tm_next = threadmap_freelist;
      threadmap_freelist = tme;
    }
  }
}


void fs_usage_fd_set(uintptr_t thread, unsigned int fd) {
  threadmap_t tme;

  if ((tme = find_map_entry(thread)) == 0) {
    return;
  }
  /*
   * If the map is not allocated, then now is the time
   */
  if (tme->tm_setptr == (unsigned long *)0) {
    if ((tme->tm_setptr = (unsigned long *)malloc(FS_USAGE_NFDBYTES(FS_USAGE_FD_SETSIZE))) == 0) {
      return;
    }

    tme->tm_setsize = FS_USAGE_FD_SETSIZE;
    bzero(tme->tm_setptr, (FS_USAGE_NFDBYTES(FS_USAGE_FD_SETSIZE)));
  }
  /*
   * If the map is not big enough, then reallocate it
   */
  while (tme->tm_setsize <= fd) {
    int n = tme->tm_setsize * 2;
    tme->tm_setptr = (unsigned long *)realloc(tme->tm_setptr, (FS_USAGE_NFDBYTES(n)));

    bzero(&tme->tm_setptr[(tme->tm_setsize/FS_USAGE_NFDBITS)], (FS_USAGE_NFDBYTES(tme->tm_setsize)));
    tme->tm_setsize = n;
  }
  /*
   * set the bit
   */
  tme->tm_setptr[fd/FS_USAGE_NFDBITS] |= (1 << ((fd) % FS_USAGE_NFDBITS));
}


/*
 * Return values:
 *  0 : File Descriptor bit is not set
 *  1 : File Descriptor bit is set
 */
int fs_usage_fd_isset(uintptr_t thread, unsigned int fd) {
  threadmap_t tme;
  int ret = 0;

  if ((tme = find_map_entry(thread))) {
    if (tme->tm_setptr && fd < tme->tm_setsize) {
      ret = tme->tm_setptr[fd/FS_USAGE_NFDBITS] & (1 << (fd % FS_USAGE_NFDBITS));
    }
  }
  return ret;
}
    

void fs_usage_fd_clear(uintptr_t thread, unsigned int fd) {
  threadmap_t tme;

  if ((tme = find_map_entry(thread))) {
    if (tme->tm_setptr && fd < tme->tm_setsize) {
      tme->tm_setptr[fd/FS_USAGE_NFDBITS] &= ~(1 << (fd % FS_USAGE_NFDBITS));
    }
  }
}



void argtopid(char *str) {
  char *cp;
  int ret = (int)strtol(str, &cp, 10);

  if (cp == str || *cp) {
    /*
     * Assume this is a command string and find matching pids
     */
    if (!kp_buffer) {
      find_proc_names();
    }

    for (int i = 0; i < kp_nentries && num_of_pids < (pids.size() - 1); i++) {
      if (kp_buffer[i].kp_proc.p_stat == 0) {
        continue;
      } else {
        if (!strncmp(str, kp_buffer[i].kp_proc.p_comm,
              sizeof(kp_buffer[i].kp_proc.p_comm) -1)) {
          pids[num_of_pids++] = kp_buffer[i].kp_proc.p_pid;
        }
      }
    }
  } else if (num_of_pids < (pids.size() - 1)) {
    pids[num_of_pids++] = ret;
  }
}

/*
 * ret = 1 means print the entry
 * ret = 0 means don't print the entry
 */

/*
 * meaning of filter flags:
 *
 * exec   show exec/posix_spawn
 * pathname show events with a pathname and close()
 * filesys  show filesystem events
 *
 * filters may be combined; default is all filters on
 */
int check_filter_mode(struct th_info *ti, int type, int error, int retval, const char *sc_name) {
  int ret = 0;
  int network_fd_isset = 0;
  unsigned int fd;

  if (filter_mode == DEFAULT_DO_NOT_FILTER) {
    return 1;
  }
  
  if (filter_mode & EXEC_FILTER) {
    if (type == BSC_execve || type == BSC_posix_spawn) {
      return 1;
    }
  }

  if (filter_mode & PATHNAME_FILTER) {
    if (ti && ti->lookups[0].pathname[0]) {
      return 1;
    }
    if (type == BSC_close || type == BSC_close_nocancel ||
        type == BSC_guarded_close_np) {
      return 1;
    }
  }

  if (ti == (struct th_info *)0) {
    if (filter_mode & FILESYS_FILTER) {
      return 1;
    }
    return 0;
  }

  switch (type) {
  case BSC_close:
  case BSC_close_nocancel:
  case BSC_guarded_close_np:
    fd = ti->arg1;
    network_fd_isset = fs_usage_fd_isset(ti->thread, fd);

    if (error == 0) {
      fs_usage_fd_clear(ti->thread, fd);
    }

    if (!network_fd_isset) {
      if (filter_mode & FILESYS_FILTER) {
        ret = 1;
      }
    }
    break;

  case BSC_read:
  case BSC_write:
  case BSC_read_nocancel:
  case BSC_write_nocancel:
    /*
     * we don't care about error in these cases
     */
    fd = ti->arg1;
    network_fd_isset = fs_usage_fd_isset(ti->thread, fd);

    if (!network_fd_isset) {
      if (filter_mode & FILESYS_FILTER) {
        ret = 1;
      }
    }
    break;

  case BSC_accept:
  case BSC_accept_nocancel:
  case BSC_socket:
    fd = retval;

    if (error == 0) {
      fs_usage_fd_set(ti->thread, fd);
    }
    break;

  case BSC_recvfrom:
  case BSC_sendto:
  case BSC_recvmsg:
  case BSC_sendmsg:
  case BSC_connect:
  case BSC_bind:
  case BSC_listen:      
  case BSC_sendto_nocancel:
  case BSC_recvfrom_nocancel:
  case BSC_recvmsg_nocancel:
  case BSC_sendmsg_nocancel:
  case BSC_connect_nocancel:
    fd = ti->arg1;

    if (error == 0) {
      fs_usage_fd_set(ti->thread, fd);
    }
    break;

  case BSC_dup:
  case BSC_dup2:
    /*
     * We track these cases for fd state only
     */
    fd = ti->arg1;
    network_fd_isset = fs_usage_fd_isset(ti->thread, fd);

    if (error == 0 && network_fd_isset) {
      /*
       * then we are duping a socket descriptor
       */
      fd = retval;  /* the new fd */
      fs_usage_fd_set(ti->thread, fd);
    }
    break;

  default:
    if (filter_mode & FILESYS_FILTER) {
      ret = 1;
    }
    break;
  }

  return ret;
}

/*
 * Allocate a buffer that is large enough to hold the maximum arguments
 * to execve().  This is used when getting the arguments to programs
 * when we see LaunchCFMApps.  If this fails, it is not fatal, we will
 * simply not resolve the command name.
 */

/* TODO(peck): We don't need to track command names really */
void init_arguments_buffer() {
  int mib[2];
  size_t size;

  mib[0] = CTL_KERN;
  mib[1] = KERN_ARGMAX;
  size = sizeof(argmax);

  if (sysctl(mib, 2, &argmax, &size, NULL, 0) == -1) {
    return;
  }
#if 1
  /* Hack to avoid kernel bug. */
  if (argmax > 8192) {
    argmax = 8192;
  }
#endif
  arguments = (char *)malloc(argmax);
}


/* TODO(peck): We don't need to track command names really */
int get_real_command_name(int pid, char *cbuf, int csize) {
  /*
   * Get command and arguments.
   */
  char *cp;
  int mib[4];
  char *command_beg, *command, *command_end;

  if (cbuf == NULL) {
    return 0;
  }

  if (arguments) {
    bzero(arguments, argmax);
  } else {
    return 0;
  }

  /*
   * A sysctl() is made to find out the full path that the command
   * was called with.
   */
  mib[0] = CTL_KERN;
  mib[1] = KERN_PROCARGS2;
  mib[2] = pid;
  mib[3] = 0;

  if (sysctl(mib, 3, arguments, (size_t *)&argmax, NULL, 0) < 0) {
    return 0;
  }

  /*
   * Skip the saved exec_path
   */
  for (cp = arguments; cp < &arguments[argmax]; cp++) {
    if (*cp == '\0') {
      /*
       * End of exec_path reached
       */
      break;
    }
  }
  if (cp == &arguments[argmax]) {
    return 0;
  }

  /*
   * Skip trailing '\0' characters
   */
  for (; cp < &arguments[argmax]; cp++) {
    if (*cp != '\0') {
      /*
       * Beginning of first argument reached
       */
      break;
    }
  }
  if (cp == &arguments[argmax]) {
    return 0;
  }

  command_beg = cp;
  /*
   * Make sure that the command is '\0'-terminated.  This protects
   * against malicious programs; under normal operation this never
   * ends up being a problem..
   */
  for (; cp < &arguments[argmax]; cp++) {
    if (*cp == '\0') {
      /*
       * End of first argument reached
       */
      break;
    }
  }
  if (cp == &arguments[argmax]) {
    return 0;
  }

  command_end = command = cp;

  /*
   * Get the basename of command
   */
  for (command--; command >= command_beg; command--) {
    if (*command == '/') {
      command++;
      break;
    }
  }
  strncpy(cbuf, (char *)command, csize);
  cbuf[csize-1] = '\0';

  return 1;
}
