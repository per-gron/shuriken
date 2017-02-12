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

#include <tuple>
#include <unordered_map>
#include <vector>

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

#include "syscall_constants.h"
#include "syscall_tables.h"


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

struct lookup {
  uintptr_t pathname[NUMPARMS + 1]; /* add room for null terminator */
};

struct th_info {
  th_info *next;
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


struct threadmap_entry {
  unsigned int tm_setsize = 0; /* this is a bit count */
  unsigned long *tm_setptr = nullptr;  /* file descripter bitmap */
  char tm_command[MAXCOMLEN + 1];
};


struct vnode_info {
  vnode_info *vn_next;
  uint64_t vn_id;
  uintptr_t vn_pathname[NUMPARMS + 1];
};

struct meta_info {
  meta_info *m_next;
  uint64_t m_blkno;
  const char *m_nameptr;
};

#define HASH_SIZE 1024
#define HASH_MASK (HASH_SIZE - 1)

th_info *th_info_hash[HASH_SIZE];
th_info *th_info_freelist;

std::unordered_map<uintptr_t, threadmap_entry> threadmap;


#define VN_HASH_SHIFT 3
#define VN_HASH_SIZE 16384
#define VN_HASH_MASK (VN_HASH_SIZE - 1)

vnode_info *vn_info_hash[VN_HASH_SIZE];
meta_info *m_info_hash[VN_HASH_SIZE];


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

#define NFS_DEV -1
#define CS_DEV -2

static constexpr int MACH_vmfault = 0x01300008;
static constexpr int MACH_pageout = 0x01300004;
static constexpr int VFS_ALIAS_VP = 0x03010094;

static constexpr int BSC_thread_terminate = 0x040c05a4;

static constexpr int HFS_update = 0x3018000;
static constexpr int HFS_modify_block_end = 0x3018004;

static constexpr int Throttled = 0x3010184;
static constexpr int SPEC_unmap_info = 0x3060004;
static constexpr int proc_exit = 0x4010004;

extern "C" int reexec_to_match_kernel();

void    format_print(th_info *, const char *, uintptr_t, int, uintptr_t, uintptr_t, uintptr_t, uintptr_t, Fmt, int, const char *);
void    enter_event_now(uintptr_t, int, kd_buf *, const char *);
void    enter_event(uintptr_t thread, int type, kd_buf *kd, const char *name);
void    exit_event(const char *, uintptr_t, int, uintptr_t, uintptr_t, uintptr_t, uintptr_t, Fmt);

void    fs_usage_fd_set(uintptr_t, unsigned int);
int     fs_usage_fd_isset(uintptr_t, unsigned int);
void    fs_usage_fd_clear(uintptr_t, unsigned int);

void    init_arguments_buffer();
int     get_real_command_name(int, char *, int);

void    delete_all_events();
void    delete_event(th_info *);
th_info *add_event(uintptr_t, int);
th_info *find_event(uintptr_t, int);

void    read_command_map();
void    create_map_entry(uintptr_t, int, char *);

char   *add_vnode_name(uint64_t, const char *);
const char *find_vnode_name(uint64_t);
void    add_meta_name(uint64_t, const char *);

void    argtopid(char *str);
void    set_remove();
void    set_pidcheck(int pid, int on_off);
void    set_pidexclude(int pid, int on_off);
int     quit(const char *s);

static const auto bsd_syscalls = make_bsd_syscall_table();
static auto filemgr_calls = make_filemgr_calls();

std::vector<int> pids;

int exclude_pids = 0;


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
    for (int pid : pids) {
      set_pidcheck(pid, 0);
    }
  } else {
    for (int pid : pids) {
      set_pidexclude(pid, 0);
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

  fprintf(stderr, "Usage: %s [-e] [pid [pid] ...]\n", myname);
  fprintf(stderr, "  -e    exclude the specified list of pids from the sample\n");
  fprintf(stderr, "        and exclude fs_usage by default\n");
  fprintf(stderr, "  pid   selects process(s) to sample\n");

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
    pids.push_back(getpid());
  }

  while (argc > 0) {
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
    for (int pid : pids) {
      set_pidcheck(pid, 1);
    }
  } else {
    for (int pid : pids) {
      set_pidexclude(pid, 1);
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
    th_info *ti;


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
      if ((ti = find_event(thread, TRACE_DATA_NEWTHREAD)) == nullptr) {
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
          exit_event("execve", thread, BSC_execve, 0, 0, 0, 0, Fmt::DEFAULT);
        }
      } else if ((ti = find_event(thread, BSC_posix_spawn))) {
        if (ti->lookups[0].pathname[0]) {
          exit_event("posix_spawn", thread, BSC_posix_spawn, 0, 0, 0, 0, Fmt::DEFAULT);
        }
      }
      if ((ti = find_event(thread, TRACE_DATA_EXEC)) == nullptr) {
        continue;
      }

      create_map_entry(thread, ti->pid, (char *)&kd[i].arg1);

      delete_event(ti);
      continue;

    case BSC_thread_terminate:
      threadmap.erase(thread);
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
      if ((ti = find_event(thread, 0)) == nullptr) {
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
       exit_event("  THROTTLED", thread, type, 0, 0, 0, 0, Fmt::DEFAULT);
       continue;

    case HFS_update:
       exit_event("  HFS_update", thread, type, kd[i].arg1, kd[i].arg2, 0, 0, Fmt::HFS_update);
       continue;

    case SPEC_unmap_info:
     format_print(NULL, "  TrimExtent", thread, type, kd[i].arg1, kd[i].arg2, kd[i].arg3, 0, Fmt::UNMAP_INFO, 0, "");
     continue;

    case MACH_pageout:
    case MACH_vmfault:
      /* TODO(peck): what about deleting all of the events? */
      if ((ti = find_event(thread, type))) {
        delete_event(ti);
      }
      continue;

    case MSC_map_fd:
      exit_event("map_fd", thread, type, kd[i].arg1, kd[i].arg2, 0, 0, Fmt::FD);
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
          threadmap.erase(thread);
        }
      }
    } else if ((type & CLASS_MASK) == FILEMGR_BASE) {
    
      if ((index = filemgr_index(type)) >= MAX_FILEMGR) {
        continue;
      }

      if (filemgr_calls[index].fm_name) {
        exit_event(filemgr_calls[index].fm_name, thread, type, kd[i].arg1, kd[i].arg2, kd[i].arg3, kd[i].arg4,
             Fmt::DEFAULT);
      }
    }
  }
  fflush(0);
}


void enter_event_now(uintptr_t thread, int type, kd_buf *kd, const char *name) {
  th_info *ti;
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

    auto tme_it = threadmap.find(thread);
    if (tme_it != threadmap.end()) {
      sprintf(buf, "  %-25.25s ", name);
      int nmclen = strlen(buf);
      printf("%s", buf);

      sprintf(buf, "(%d, 0x%lx, 0x%lx, 0x%lx)", (short)kd->arg1, kd->arg2, kd->arg3, kd->arg4);
      int argsclen = strlen(buf);

      printf("%s", buf);   /* print the kdargs */
      printf("%s.%d\n", tme_it->second.tm_command, (int)thread);
    } else {
      printf("  %-24.24s (%5d, %#lx, 0x%lx, 0x%lx)\n",         name, (short)kd->arg1, kd->arg2, kd->arg3, kd->arg4);
    }
  }
}


void enter_event(uintptr_t thread, int type, kd_buf *kd, const char *name) {
  switch (type) {

  case MSC_map_fd:
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


void exit_event(
    const char *sc_name,
    uintptr_t thread,
    int type,
    uintptr_t arg1,
    uintptr_t arg2,
    uintptr_t arg3,
    uintptr_t arg4,
    Fmt format) {
  th_info *ti;
      
  if ((ti = find_event(thread, type)) == nullptr) {
    return;
  }

  ti->nameptr = 0;

  format_print(ti, sc_name, thread, type, arg1, arg2, arg3, arg4, format, ti->waited, (char *)&ti->lookups[0].pathname[0]);

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
    th_info *ti,
    const char *sc_name,
    uintptr_t thread,
    int type,
    uintptr_t arg1,
    uintptr_t arg2,
    uintptr_t arg3,
    uintptr_t arg4,
    Fmt format,
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

  klass = type >> 24;

  threadmap_entry *tme;

  auto tme_it = threadmap.find(thread);
  if (tme_it != threadmap.end()) {
    command_name = tme_it->second.tm_command;
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
    case Fmt::AT:
    case Fmt::RENAMEAT:
    case Fmt::DEFAULT:
      /*
       * pathname based system calls or 
       * calls with no fd or pathname (i.e.  sync)
       */
      if (arg1)
        printf("      [%3lu]       ", arg1);
      else
        printf("                  ");
      break;


    case Fmt::HFS_update:
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

    case Fmt::TRUNC:
    case Fmt::FTRUNC:
      /*
       * ftruncate, truncate
       */
      if (format == Fmt::FTRUNC) {
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

    case Fmt::FCHFLAGS:
    case Fmt::CHFLAGS:
    {
      /*
       * fchflags, chflags
       */

      if (format == Fmt::FCHFLAGS) {
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

      nopadding = 1;
      break;
    }

    case Fmt::FCHMOD:
    case Fmt::FCHMOD_EXT:
    case Fmt::CHMOD:
    case Fmt::CHMOD_EXT:
    case Fmt::CHMODAT:
    {
      /*
       * fchmod, fchmod_extended, chmod, chmod_extended
       */
      if (format == Fmt::FCHMOD || format == Fmt::FCHMOD_EXT) {
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

      break;
    }

    case Fmt::ACCESS:
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

    case Fmt::OPENAT:
    case Fmt::OPEN:
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

    case Fmt::FD:
    case Fmt::FD_2:  // accept, dup, dup2
    case Fmt::FD_IO:  // system calls with fd's that return an I/O completion count
    case Fmt::UNMAP_INFO:
      printf("TODO: Not handled");
      break;
    }
  }

  if (framework_name) {
    len = sprintf(&buf[0], " %s %s ", framework_type, framework_name);
  } else if (*pathname != '\0') {
    switch(format) {
    case Fmt::AT:
    case Fmt::OPENAT:
    case Fmt::CHMODAT:
      len = sprintf(&buf[0], " [%d]/%s ", ti->arg1, pathname);
      break;
    case Fmt::RENAMEAT:
      len = sprintf(&buf[0], " [%d]/%s ", ti->arg3, pathname);
      break;
    default:
      len = sprintf(&buf[0], " %s ", pathname);
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
  meta_info *mi;

  int hashid = blockno & VN_HASH_MASK;

  for (mi = m_info_hash[hashid]; mi; mi = mi->m_next) {
    if (mi->m_blkno == blockno) {
      break;
    }
  }
  if (mi == NULL) {
    mi = reinterpret_cast<meta_info *>(malloc(sizeof(struct meta_info)));
    
    mi->m_next = m_info_hash[hashid];
    m_info_hash[hashid] = mi;
    mi->m_blkno = blockno;
  }
  mi->m_nameptr = pathname;
}

char *add_vnode_name(uint64_t vn_id, const char *pathname) {
  vnode_info *vn;

  int hashid = (vn_id >> VN_HASH_SHIFT) & VN_HASH_MASK;

  for (vn = vn_info_hash[hashid]; vn; vn = vn->vn_next) {
    if (vn->vn_id == vn_id) {
      break;
    }
  }
  if (vn == NULL) {
    vn = reinterpret_cast<vnode_info *>(malloc(sizeof(struct vnode_info)));
    
    vn->vn_next = vn_info_hash[hashid];
    vn_info_hash[hashid] = vn;
    vn->vn_id = vn_id;
  }
  strcpy(reinterpret_cast<char *>(vn->vn_pathname), pathname);

  return reinterpret_cast<char *>(&vn->vn_pathname);
}


const char *find_vnode_name(uint64_t vn_id) {
  int hashid = (vn_id >> VN_HASH_SHIFT) & VN_HASH_MASK;

  for (vnode_info *vn = vn_info_hash[hashid]; vn; vn = vn->vn_next) {
    if (vn->vn_id == vn_id) {
      return reinterpret_cast<char *>(vn->vn_pathname);
    }
  }
  return "";
}


void delete_event(th_info *ti_to_delete) {
  th_info *ti;
  th_info *ti_prev;

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

th_info *add_event(uintptr_t thread, int type) {
  th_info *ti;

  if ((ti = th_info_freelist)) {
    th_info_freelist = ti->next;
  } else {
    ti = reinterpret_cast<th_info *>(malloc(sizeof(th_info)));
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

th_info *find_event(uintptr_t thread, int type) {
  int hashid = thread & HASH_MASK;

  for (th_info *ti = th_info_hash[hashid]; ti; ti = ti->next) {
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
  return nullptr;
}

void delete_all_events() {
  th_info *ti_next = nullptr;

  for (int i = 0; i < HASH_SIZE; i++) {
    for (th_info *ti = th_info_hash[i]; ti; ti = ti_next) {
      ti_next = ti->next;
      ti->next = th_info_freelist;
      th_info_freelist = ti;
    }
    th_info_hash[i] = 0;
  }
}

void read_command_map() {
  kd_threadmap *mapptr = 0;

  threadmap.clear();

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


void create_map_entry(uintptr_t thread, int pid, char *command) {
  auto &tme = threadmap[thread];

  strncpy(tme.tm_command, command, MAXCOMLEN);
  tme.tm_command[MAXCOMLEN] = '\0';

  if (pid != 0 && pid != 1) {
    if (!strncmp(command, "LaunchCFMA", 10)) {
      (void)get_real_command_name(pid, tme.tm_command, MAXCOMLEN);
    }
  }
}


void fs_usage_fd_set(uintptr_t thread, unsigned int fd) {
  auto tme_it = threadmap.find(thread);
  if (tme_it == threadmap.end()) {
    return;
  }
  auto &tme = tme_it->second;

  /*
   * If the map is not allocated, then now is the time
   */
  if (tme.tm_setptr == (unsigned long *)0) {
    if ((tme.tm_setptr = (unsigned long *)malloc(FS_USAGE_NFDBYTES(FS_USAGE_FD_SETSIZE))) == 0) {
      return;
    }

    tme.tm_setsize = FS_USAGE_FD_SETSIZE;
    bzero(tme.tm_setptr, (FS_USAGE_NFDBYTES(FS_USAGE_FD_SETSIZE)));
  }
  /*
   * If the map is not big enough, then reallocate it
   */
  while (tme.tm_setsize <= fd) {
    int n = tme.tm_setsize * 2;
    tme.tm_setptr = (unsigned long *)realloc(tme.tm_setptr, (FS_USAGE_NFDBYTES(n)));

    bzero(&tme.tm_setptr[(tme.tm_setsize/FS_USAGE_NFDBITS)], (FS_USAGE_NFDBYTES(tme.tm_setsize)));
    tme.tm_setsize = n;
  }
  /*
   * set the bit
   */
  tme.tm_setptr[fd/FS_USAGE_NFDBITS] |= (1 << ((fd) % FS_USAGE_NFDBITS));
}


/*
 * Return values:
 *  0 : File Descriptor bit is not set
 *  1 : File Descriptor bit is set
 */
int fs_usage_fd_isset(uintptr_t thread, unsigned int fd) {
  int ret = 0;

  auto it = threadmap.find(thread);
  if (it != threadmap.end()) {
    auto &tme = it->second;
    if (tme.tm_setptr && fd < tme.tm_setsize) {
      ret = tme.tm_setptr[fd/FS_USAGE_NFDBITS] & (1 << (fd % FS_USAGE_NFDBITS));
    }
  }
  return ret;
}
    

void fs_usage_fd_clear(uintptr_t thread, unsigned int fd) {
  threadmap_entry *tme;

  auto it = threadmap.find(thread);
  if (it != threadmap.end()) {
    auto &tme = it->second;
    if (tme.tm_setptr && fd < tme.tm_setsize) {
      tme.tm_setptr[fd/FS_USAGE_NFDBITS] &= ~(1 << (fd % FS_USAGE_NFDBITS));
    }
  }
}



void argtopid(char *str) {
  char *cp;
  int ret = (int)strtol(str, &cp, 10);

  pids.push_back(ret);
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
