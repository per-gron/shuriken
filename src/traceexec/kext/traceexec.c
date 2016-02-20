#include <kern/debug.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <sys/conf.h>
#include <sys/proc.h>

static int traceexec_open(dev_t dev, int flags, int devtype, struct proc *p) {
#pragma unused(dev,flags,devtype,p)
  printf("TRACEEXEC OPENED\n");
  return 0;
}

static int traceexec_close(dev_t dev, int flags, int devtype, struct proc *p) {
#pragma unused(dev,flags,devtype,p)
  return 0;
}

static int traceexec_ioctl(
    dev_t dev, u_long cmd, caddr_t data, int fflag, struct proc *p) {
#pragma unused(dev,fflag,p)
  return 0;
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

/* let the kernel pick the device number */
static const int kTraceexecMajor = -24;

kern_return_t traceexec_start(kmod_info_t * ki, void *d) {
  if (0 >= traceexec_majdevno) {
    traceexec_majdevno = cdevsw_add(kTraceexecMajor, &traceexec_cdevsw);

    if (traceexec_majdevno < 0) {
      printf("traceexec_init: failed to allocate a major number!\n");
      return KERN_FAILURE;
    }
  } else {
    panic("traceexec_init: called twice!\n");
  }

  printf("TRACEEXEC STARTED\n");
  return KERN_SUCCESS;
}

kern_return_t traceexec_stop(kmod_info_t *ki, void *d) {
  if (0 < traceexec_majdevno) {
    cdevsw_remove(traceexec_majdevno, &traceexec_cdevsw);
    traceexec_majdevno = 0;
  }

  return KERN_SUCCESS;
}
