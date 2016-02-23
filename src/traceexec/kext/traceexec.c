#include <kern/debug.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <sys/errno.h>
#include <sys/kern_control.h>

#include "traceexec_cmds.h"

static kern_ctl_ref traceexec_kctlref = 0;

struct {
  uint32_t major;
  uint32_t minor;
  uint32_t micro;
} traceexec_version = { 1, 0, 0 };

errno_t traceexec_setopt(
    kern_ctl_ref ctlref,
    unsigned int unit,
    void *userdata,
    int opt,
    void *data,
    size_t len) {
  switch (opt) {
  case kTraceexecStartTracing:
    printf("traceexec_setopt: start tracing, len %lu\n", len);
    return 0;

  case kTraceexecStopTracing:
    printf("traceexec_setopt: stop tracing, len %lu\n", len);
    return 0;

  default:
    return EINVAL;
  }
}

errno_t traceexec_getopt(
    kern_ctl_ref kctlref,
    u_int32_t unit,
    void *unitinfo,
    int opt,
    void *data,
    size_t *len) {
  switch (opt) {
  case kTraceexecGetVersion:
    if (*len != sizeof(traceexec_version)) {
      return EINVAL;
    }
    memcpy(data, &traceexec_version, sizeof(traceexec_version));
    return 0;

  default:
    return EINVAL;
  }
}

errno_t traceexec_connect(
    kern_ctl_ref ctlref,
    struct sockaddr_ctl *sac,
    void **unitinfo) {
  return 0;
}

errno_t traceexec_disconnect(
    kern_ctl_ref ctlref,
    unsigned int unit,
    void *unitinfo) {
  return 0;
}

kern_return_t traceexec_start(kmod_info_t * ki, void *d) {
  if (traceexec_kctlref != 0) {
    panic("traceexec_init: called twice!\n");
  }

  struct kern_ctl_reg ep_ctl;
  bzero(&ep_ctl, sizeof(ep_ctl));
  strncpy(ep_ctl.ctl_name, kTraceexecControlName, sizeof(ep_ctl.ctl_name));
  ep_ctl.ctl_setopt = traceexec_setopt;
  ep_ctl.ctl_getopt = traceexec_getopt;
  ep_ctl.ctl_connect = traceexec_connect;
  ep_ctl.ctl_disconnect = traceexec_disconnect;
  if (ctl_register(&ep_ctl, &traceexec_kctlref) != 0) {
    printf("traceexec_init: failed to register kernel ctl!");
    return KERN_FAILURE;
  }

  return KERN_SUCCESS;
}

kern_return_t traceexec_stop(kmod_info_t *ki, void *d) {
  if (traceexec_kctlref != 0) {
    errno_t result = ctl_deregister(traceexec_kctlref);
    if (result == EBUSY) {
      // Refuse unload
      return KERN_FAILURE;
    } else if (result != 0) {
      panic("traceexec_stop: failed to deregeister kernel ctl!");
    }
    traceexec_kctlref = 0;
  }

  return KERN_SUCCESS;
}
