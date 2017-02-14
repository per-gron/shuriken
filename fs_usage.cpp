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

#include <string>
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

struct event_info {
  event_info *next;
  uintptr_t thread;

  uintptr_t child_thread;
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
  uint64_t vnodeid;
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

class event_info_map {
 public:
  void delete_all_events();
  void delete_event(event_info *);
  event_info *add_event(uintptr_t, int);
  event_info *find_event(uintptr_t, int);

 private:
  static constexpr int HASH_SIZE = 1024;
  static constexpr int HASH_MASK = HASH_SIZE - 1;

  std::array<event_info *, HASH_SIZE> _event_info_hash;
  event_info *_event_info_freelist = nullptr;
};

std::unordered_map<uintptr_t, threadmap_entry> threadmap;
std::unordered_map<uint64_t, std::string> vn_name_map;
event_info_map ei_map;


int need_new_map = 1;  /* TODO(peck): This should be treated as an error instead. */

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

void    format_print(event_info *, const char *, uintptr_t, int, uintptr_t, uintptr_t, uintptr_t, uintptr_t, Fmt, const char *);
void    enter_event_now(uintptr_t, int, kd_buf *, const char *);
void    enter_event(uintptr_t thread, int type, kd_buf *kd, const char *name);
void    enter_illegal_event(uintptr_t thread, int type);
void    exit_event(const char *, uintptr_t, int, uintptr_t, uintptr_t, uintptr_t, uintptr_t, Fmt);

void    fs_usage_fd_set(uintptr_t, unsigned int);
int     fs_usage_fd_isset(uintptr_t, unsigned int);
void    fs_usage_fd_clear(uintptr_t, unsigned int);

void    init_arguments_buffer();
int     get_real_command_name(int, char *, int);

void    read_command_map();
void    create_map_entry(uintptr_t, int, char *);

void    argtopid(char *str);
void    set_remove();
void    set_pidcheck(int pid, int on_off);
void    set_pidexclude(int pid, int on_off);
int     quit(const char *s);

static const auto bsd_syscalls = make_bsd_syscall_table();

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

    ei_map.delete_all_events();

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
    event_info *ei;


    thread  = kd[i].arg5;
    debugid = kd[i].debugid;
    type    = kd[i].debugid & DBG_FUNC_MASK;

    switch (type) {
    case TRACE_DATA_NEWTHREAD:
      if (kd[i].arg1) {
        if ((ei = ei_map.add_event(thread, TRACE_DATA_NEWTHREAD)) == NULL) {
          continue;
        }
        ei->child_thread = kd[i].arg1;
        ei->pid = kd[i].arg2;
        /* TODO(peck): Removeme */
        /* printf("newthread PID %d (thread = %d, child_thread = %d)\n", (int)ei->pid, (int)thread, ei->child_thread); */
      }
      continue;

    case TRACE_STRING_NEWTHREAD:
      if ((ei = ei_map.find_event(thread, TRACE_DATA_NEWTHREAD)) == nullptr) {
        continue;
      }

      create_map_entry(ei->child_thread, ei->pid, (char *)&kd[i].arg1);

      ei_map.delete_event(ei);
      continue;
  
    case TRACE_DATA_EXEC:
      if ((ei = ei_map.add_event(thread, TRACE_DATA_EXEC)) == NULL) {
        continue;
      }

      ei->pid = kd[i].arg1;
      continue;

    case TRACE_STRING_EXEC:
      if ((ei = ei_map.find_event(thread, BSC_execve))) {
        if (ei->lookups[0].pathname[0]) {
          exit_event("execve", thread, BSC_execve, 0, 0, 0, 0, Fmt::DEFAULT);
        }
      } else if ((ei = ei_map.find_event(thread, BSC_posix_spawn))) {
        if (ei->lookups[0].pathname[0]) {
          exit_event("posix_spawn", thread, BSC_posix_spawn, 0, 0, 0, 0, Fmt::DEFAULT);
        }
      }
      if ((ei = ei_map.find_event(thread, TRACE_DATA_EXEC)) == nullptr) {
        continue;
      }

      create_map_entry(thread, ei->pid, (char *)&kd[i].arg1);

      ei_map.delete_event(ei);
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

    case VFS_ALIAS_VP:
      {
        auto name_it = vn_name_map.find(kd[i].arg1);
        if (name_it != vn_name_map.end()) {
          vn_name_map[kd[i].arg2] = name_it->second;
        } else {
          // TODO(peck): Can this happen?
          vn_name_map.erase(kd[i].arg2);
        }
        continue;
      }

    case VFS_LOOKUP:
      if ((ei = ei_map.find_event(thread, 0)) == nullptr) {
        continue;
      }

      if (debugid & DBG_FUNC_START) {

        if (ei->type == HFS_update) {
          ei->pn_work_index = (MAX_PATHNAMES - 1);
        } else {
          if (ei->pn_scall_index < MAX_SCALL_PATHNAMES) {
            ei->pn_work_index = ei->pn_scall_index;
          } else {
            continue;
          }
        }
        sargptr = &ei->lookups[ei->pn_work_index].pathname[0];

        ei->vnodeid = kd[i].arg1;

        *sargptr++ = kd[i].arg2;
        *sargptr++ = kd[i].arg3;
        *sargptr++ = kd[i].arg4;
        /*
         * NULL terminate the 'string'
         */
        *sargptr = 0;

        ei->pathptr = sargptr;
      } else {
        sargptr = ei->pathptr;

        /*
         * We don't want to overrun our pathname buffer if the
         * kernel sends us more VFS_LOOKUP entries than we can
         * handle and we only handle 2 pathname lookups for
         * a given system call
         */
        if (sargptr == 0) {
          continue;
        }

        if ((uintptr_t)sargptr < (uintptr_t)&ei->lookups[ei->pn_work_index].pathname[NUMPARMS]) {

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

        vn_name_map[ei->vnodeid] =
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

    if (debugid & DBG_FUNC_START) {
      if ((type & CLASS_MASK) == FILEMGR_BASE) {
        enter_illegal_event(thread, type);
      } else {
        enter_event(thread, type, &kd[i], nullptr);
      }
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
     format_print(NULL, "  TrimExtent", thread, type, kd[i].arg1, kd[i].arg2, kd[i].arg3, 0, Fmt::UNMAP_INFO, nullptr);
     continue;

    case MACH_pageout:
    case MACH_vmfault:
      /* TODO(peck): what about deleting all of the events? */
      if ((ei = ei_map.find_event(thread, type))) {
        ei_map.delete_event(ei);
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
    }
  }
  fflush(0);
}


void enter_event_now(uintptr_t thread, int type, kd_buf *kd, const char *name) {
  event_info *ei;
  char buf[MAXWIDTH];
  buf[0] = 0;

  if ((ei = ei_map.add_event(thread, type)) == NULL) {
    return;
  }

  ei->arg1 = kd->arg1;
  ei->arg2 = kd->arg2;
  ei->arg3 = kd->arg3;
  ei->arg4 = kd->arg4;
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
}


void enter_illegal_event(uintptr_t thread, int type) {
  // TODO(peck): Only exist if the thread is one that is traced
  fprintf(stderr, "Encountered illegal syscall (perhaps a Carbon File Manager)\n");
  exit(1);
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
  event_info *ei;
      
  if ((ei = ei_map.find_event(thread, type)) == nullptr) {
    return;
  }

  format_print(ei, sc_name, thread, type, arg1, arg2, arg3, arg4, format, (char *)&ei->lookups[0].pathname[0]);
  ei_map.delete_event(ei);
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
    event_info *ei,
    const char *sc_name,
    uintptr_t thread,
    int type,
    uintptr_t arg1,
    uintptr_t arg2,
    uintptr_t arg3,
    uintptr_t arg4,
    Fmt format,
    const char *pathname /* nullable */) {
  char buf[MAXWIDTH];

  threadmap_entry *tme;

  auto tme_it = threadmap.find(thread);
  const char *command_name = tme_it == threadmap.end() ?
      "" : tme_it->second.tm_command;

  printf("  %-17.17s", sc_name);

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

    if (sflag & 0x10) {
      sbuf[0] = 'F';
    }
    if (sflag & 0x08) {
      sbuf[1] = 'M';
    }
    if (sflag & 0x20) {
      sbuf[2] = 'D';
    }
    if (sflag & 0x04) {
      sbuf[3] = 'c';
    }
    if (sflag & 0x01) {
      sbuf[4] = 'a';
    }
    if (sflag & 0x02) {
      sbuf[5] = 'm';
    }

    printf("            (%s) ", sbuf);

    auto name_it = vn_name_map.find(arg1);
    pathname = name_it == vn_name_map.end() ? nullptr : name_it->second.c_str();

    break;
  }

  case Fmt::TRUNC:
  case Fmt::FTRUNC:
    /*
     * ftruncate, truncate
     */
    if (format == Fmt::FTRUNC) {
      printf(" F=%-3d", ei->arg1);
    } else {
      printf("      ");
    }

    if (arg1) {
      printf("[%3lu]", arg1);
    }

#ifdef __ppc__
    offset_reassembled = (((off_t)(unsigned int)(ei->arg2)) << 32) | (unsigned int)(ei->arg3);
#else
    offset_reassembled = (((off_t)(unsigned int)(ei->arg3)) << 32) | (unsigned int)(ei->arg2);
#endif
    clip_64bit("  O=", offset_reassembled);

    break;

  case Fmt::FCHFLAGS:
  case Fmt::CHFLAGS:
  {
    /*
     * fchflags, chflags
     */

    if (format == Fmt::FCHFLAGS) {
      if (arg1) {
        printf(" F=%-3d[%3lu]", ei->arg1, arg1);
      } else {
        printf(" F=%-3d", ei->arg1);
      }
    } else {
      if (arg1) {
        printf(" [%3lu] ", arg1);
      }
    }

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
        printf(" F=%-3d[%3lu] ", ei->arg1, arg1);
      } else {
        printf(" F=%-3d ", ei->arg1);
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

    if (ei->arg2 & R_OK) {
      mode[0] = 'R';
    }
    if (ei->arg2 & W_OK) {
      mode[1] = 'W';
    }
    if (ei->arg2 & X_OK) {
      mode[2] = 'X';
    }
    if (ei->arg2 == F_OK) {
      mode[3] = 'F';
    }

    if (arg1) {
      printf("      [%3lu] (%s)   ", arg1, mode);
    } else {
      printf("            (%s)   ", mode);
    }

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

    if (ei->arg2 & O_RDWR) {
      mode[0] = 'R';
      mode[1] = 'W';
    } else if (ei->arg2 & O_WRONLY) {
      mode[1] = 'W';
    } else {
      mode[0] = 'R';
    }

    if (ei->arg2 & O_CREAT) {
      mode[2] = 'C';
    }

    if (ei->arg2 & O_APPEND) {
      mode[3] = 'A';
    }

    if (ei->arg2 & O_TRUNC) {
      mode[4] = 'T';
    }

    if (ei->arg2 & O_EXCL) {
      mode[5] = 'E';
    }

    if (arg1) {
      printf("      [%3lu] (%s) ", arg1, mode);
    } else {
      printf(" F=%-3lu      (%s) ", arg2, mode);
    }

    break;
  }

  case Fmt::FD:
  case Fmt::FD_2:  // accept, dup, dup2
  case Fmt::FD_IO:  // system calls with fd's that return an I/O completion count
  case Fmt::UNMAP_INFO:
    printf("TODO: Not handled");
    break;
  }

  if (pathname) {
    switch(format) {
    case Fmt::AT:
    case Fmt::OPENAT:
    case Fmt::CHMODAT:
      sprintf(&buf[0], " [%d]/%s ", ei->arg1, pathname);
      break;
    case Fmt::RENAMEAT:
      sprintf(&buf[0], " [%d]/%s ", ei->arg3, pathname);
      break;
    default:
      sprintf(&buf[0], " %s ", pathname);
    }
  }

  pathname = buf;

  printf("%s %s.%d\n", pathname, command_name, (int)thread);
}


void event_info_map::delete_event(event_info *ei_to_delete) {
  event_info *ei;
  event_info *ei_prev;

  int hashid = ei_to_delete->thread & HASH_MASK;

  if ((ei = _event_info_hash[hashid])) {
    if (ei == ei_to_delete) {
      _event_info_hash[hashid] = ei->next;
    } else {
      ei_prev = ei;

      for (ei = ei->next; ei; ei = ei->next) {
        if (ei == ei_to_delete) {
          ei_prev->next = ei->next;
          break;
        }
        ei_prev = ei;
      }
    }
    if (ei) {
      ei->next = _event_info_freelist;
      _event_info_freelist = ei;
    }
  }
}

event_info *event_info_map::add_event(uintptr_t thread, int type) {
  event_info *ei;

  if ((ei = _event_info_freelist)) {
    _event_info_freelist = ei->next;
  } else {
    ei = reinterpret_cast<event_info *>(malloc(sizeof(event_info)));
  }

  int hashid = thread & HASH_MASK;

  ei->next = _event_info_hash[hashid];
  _event_info_hash[hashid] = ei;

  ei->thread = thread;
  ei->type = type;

  ei->pathptr = &ei->lookups[0].pathname[0];
  ei->pn_scall_index = 0;
  ei->pn_work_index = 0;

  for (int i = 0; i < MAX_PATHNAMES; i++) {
    ei->lookups[i].pathname[0] = 0;
  }

  return ei;
}

event_info *event_info_map::find_event(uintptr_t thread, int type) {
  int hashid = thread & HASH_MASK;

  for (event_info *ei = _event_info_hash[hashid]; ei; ei = ei->next) {
    if (ei->thread == thread) {
      if (type == ei->type) {
        return ei;
      }
      if (type == 0) {
        return ei;
      }
    }
  }
  return nullptr;
}

void event_info_map::delete_all_events() {
  event_info *ei_next = nullptr;

  for (int i = 0; i < HASH_SIZE; i++) {
    for (event_info *ei = _event_info_hash[i]; ei; ei = ei_next) {
      ei_next = ei->next;
      ei->next = _event_info_freelist;
      _event_info_freelist = ei;
    }
    _event_info_hash[i] = 0;
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
