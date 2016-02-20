#include <kern/debug.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <miscfs/devfs/devfs.h>
#include <sys/conf.h>
#include <sys/proc.h>

#include "traceexec_cmds.h"

struct {
  uint32_t major;
  uint32_t minor;
  uint32_t micro;
} traceexec_version = { .major = 1, .minor = 0, .micro = 0 };

static int traceexec_open(dev_t dev, int flags, int devtype, struct proc *p) {
  printf("TRACEEXEC OPENED\n");
  return 0;
}

static int traceexec_close(dev_t dev, int flags, int devtype, struct proc *p) {
  return 0;
}

static int traceexec_ioctl(
    dev_t dev, u_long cmd, caddr_t data, int fflag, struct proc *p) {
  switch (cmd) {
  case TRACEEXEC_GET_VERSION:
    copyout(&traceexec_version, (user_addr_t)data, sizeof(traceexec_version));
    return 0;
  default:
    return ENOTTY;
  }
}

static struct cdevsw traceexec_cdevsw =
{
  traceexec_open,           /* open */
  traceexec_close,          /* close */
  eno_rdwrt,                /* read */
  eno_rdwrt,                /* write */
  traceexec_ioctl,          /* ioctl */
  (stop_fcn_t *)eno_stop,   /* stop */
  (reset_fcn_t *)eno_reset, /* reset */
  NULL,                     /* tty's */
  eno_select,               /* select */
  eno_mmap,                 /* mmap */
  eno_strat,                /* strategy */
  eno_getc,                 /* getc */
  eno_putc,                 /* putc */
  0                         /* type */
};

static int traceexec_majdevno = 0;
static void *traceexec_devfs = NULL;

/* let the kernel pick the device number */
static const int kTraceexecMajor = -24;

kern_return_t traceexec_start(kmod_info_t * ki, void *d) {
  if (traceexec_majdevno > 0 || traceexec_devfs != NULL) {
    panic("traceexec_init: called twice!\n");
  }

  traceexec_majdevno = cdevsw_add(kTraceexecMajor, &traceexec_cdevsw);
  if (traceexec_majdevno < 0) {
    printf("traceexec_init: failed to allocate a major number\n");
    return KERN_FAILURE;
  }

  traceexec_devfs = devfs_make_node(
      makedev(traceexec_majdevno, 0),
      DEVFS_CHAR,
      UID_ROOT,
      GID_WHEEL,
      0666,
      "traceexec",
      0);
  if (traceexec_devfs == NULL) {
    printf("traceexec_init: failed to devfs_make_node\n");
    return KERN_FAILURE;
  }

  return KERN_SUCCESS;
}

kern_return_t traceexec_stop(kmod_info_t *ki, void *d) {
  if (traceexec_devfs != NULL) {
    devfs_remove(traceexec_devfs);
    traceexec_devfs = NULL;
  }

  if (0 < traceexec_majdevno) {
    cdevsw_remove(traceexec_majdevno, &traceexec_cdevsw);
    traceexec_majdevno = 0;
  }

  return KERN_SUCCESS;
}
