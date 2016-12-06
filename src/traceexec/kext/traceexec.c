#include <kern/debug.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <sys/errno.h>
#include <sys/kauth.h>
#include <sys/kern_control.h>
#include <sys/proc.h>
#include <sys/ucred.h>

struct auditinfo_addr;  // required to #include mac_policy.h
struct componentname;  // required to #include mac_policy.h
struct flock;  // required to #include mac_policy.h
struct knote;  // required to #include mac_policy.h
struct sockaddr;  // required to #include mac_policy.h
struct timespec;  // required to #include mac_policy.h
struct vnode_attr;  // required to #include mac_policy.h
#include <security/mac_policy.h>

#include "traceexec_cmds.h"

static int traceexec_mac_slot = 0;
static kern_ctl_ref traceexec_kctlref = 0;

struct {
  uint32_t major;
  uint32_t minor;
  uint32_t micro;
} traceexec_version = { 1, 0, 0 };

static errno_t start_tracing() {
  proc_t proc = proc_self();
  kauth_cred_t cred = kauth_cred_get_with_ref();

  if (!cred->cr_label) {
    printf("traceexec: failed to start tracing because the credential has no label\n");
    return KERN_FAILURE;
  }

  printf("traceexec_setopt: start tracing %ld\n", mac_label_get(cred->cr_label, traceexec_mac_slot));
  mac_label_set(cred->cr_label, traceexec_mac_slot, 1);
  kauth_proc_label_update(proc, cred->cr_label);

  kauth_cred_unref(&cred);
  proc_rele(proc);
  return KERN_SUCCESS;
}

static errno_t traceexec_setopt(
    kern_ctl_ref ctlref,
    unsigned int unit,
    void *userdata,
    int opt,
    void *data,
    size_t len) {
  switch (opt) {
  case kTraceexecStartTracing:
    return start_tracing();

  default:
    return EINVAL;
  }
}

static errno_t traceexec_getopt(
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

static errno_t traceexec_connect(
    kern_ctl_ref ctlref,
    struct sockaddr_ctl *sac,
    void **unitinfo) {
  return 0;
}

static errno_t traceexec_disconnect(
    kern_ctl_ref ctlref,
    unsigned int unit,
    void *unitinfo) {
  return 0;
}

static void traceexec_cred_label_destroy(struct label *label) {
  // This callback is used to detect when a traced process exits. When tracing
  // begins, traceexec attaches a label to the process. When that process goes
  // away, the label will go out of scope and this callback will be called.

  intptr_t slot = mac_label_get(label, traceexec_mac_slot);
  if (slot) {
    printf("traceexec_cred_label_destroy: called %ld\n", slot);
  }
}

static int traceexec_vnode_check_revoke(
    kauth_cred_t cred,
    struct vnode *vp,
    struct label *label) {
  printf("tracexec: check revoke %ld!\n", mac_label_get(cred->cr_label, traceexec_mac_slot));
  return 0;
}

static int traceexec_vnode_check_stat(
    struct ucred *active_cred,
    struct ucred *file_cred,  /* NULLOK */
    struct vnode *vp,
    struct label *label) {
  intptr_t lbl = mac_label_get(active_cred->cr_label, traceexec_mac_slot);
  if (lbl) {
    printf("tracexec: check stat %ld!\n", lbl);
  }
  return 0;
}

static struct mac_policy_ops traceexec_policy_ops = {
  .mpo_cred_label_destroy = &traceexec_cred_label_destroy,
//  .mpo_cred_check_label_update = ... << check that the tracing label isn't removed
//  .mpo_priv_check = ... << access check for privileged operations

//  .mpo_vnode_check_access = ... <<< access syscall
//  .mpo_vnode_check_deleteextattr = ...
//  .mpo_vnode_check_exchangedata = ...
//  .mpo_vnode_check_exec = ...
//  .mpo_vnode_check_getattrlist = ...
//  .mpo_vnode_check_getextattr = ...
//  .mpo_vnode_check_ioctl = ...
//  .mpo_vnode_check_link = ...
//  .mpo_vnode_check_listextattr = ...
//  .mpo_vnode_check_open = ...
//  .mpo_vnode_check_readdir = ...
//  .mpo_vnode_check_readlink = ...
//  .mpo_vnode_check_rename = ...
  .mpo_vnode_check_revoke = &traceexec_vnode_check_revoke,
//  .mpo_vnode_check_setattrlist = ...
//  .mpo_vnode_check_setextattr = ...
//  .mpo_vnode_check_setflags = ...
//  .mpo_vnode_check_setmode = ...
//  .mpo_vnode_check_setowner = ...
//  .mpo_vnode_check_setutimes = ...
  .mpo_vnode_check_stat = &traceexec_vnode_check_stat,
//  .mpo_vnode_check_truncate = ...
//  .mpo_vnode_check_unlink = ...
//  .mpo_vnode_check_write = ...
//  .mpo_vnode_check_uipc_connect = ...
//  .mpo_vnode_check_uipc_bind = ...
};

static struct mac_policy_conf traceexec_policy_conf = {
   .mpc_name = "traceexec",
   .mpc_fullname = "traceexec kernel extension for allowing processes to trace their own file accesses",
   .mpc_labelnames = NULL,
   .mpc_labelname_count = 0,
   .mpc_ops = &traceexec_policy_ops,
   .mpc_loadtime_flags = MPC_LOADTIME_FLAG_UNLOADOK,
   .mpc_field_off = &traceexec_mac_slot,
   .mpc_runtime_flags = 0,
   .mpc_list = NULL,
   .mpc_data = NULL
};

static mac_policy_handle_t traceexec_policy_handle = 0;

kern_return_t traceexec_start(kmod_info_t * ki, void *d) {
  if (traceexec_kctlref != 0 || traceexec_policy_handle != 0) {
    printf("traceexec_init: called twice!\n");
    return KERN_FAILURE;
  }

  struct kern_ctl_reg ep_ctl;
  bzero(&ep_ctl, sizeof(ep_ctl));
  strncpy(ep_ctl.ctl_name, kTraceexecControlName, sizeof(ep_ctl.ctl_name));
  ep_ctl.ctl_setopt = traceexec_setopt;
  ep_ctl.ctl_getopt = traceexec_getopt;
  ep_ctl.ctl_connect = traceexec_connect;
  ep_ctl.ctl_disconnect = traceexec_disconnect;
  if (ctl_register(&ep_ctl, &traceexec_kctlref) != 0) {
    printf("traceexec_init: failed to register kernel ctl!\n");
    return KERN_FAILURE;
  }

  if (mac_policy_register(&traceexec_policy_conf, &traceexec_policy_handle, d) != KERN_SUCCESS) {
    printf("traceexec_init: failed to register MAC policy!\n");
    return KERN_FAILURE;
  }

  return KERN_SUCCESS;
}

kern_return_t traceexec_stop(kmod_info_t *ki, void *d) {
  if (traceexec_kctlref != 0) {
    errno_t result = ctl_deregister(traceexec_kctlref);
    if (result == EBUSY) {
      // Refuse unload
      printf("traceexec_stop: refusing unload because there are active traces\n");
      return KERN_FAILURE;
    } else if (result != 0) {
      panic("traceexec_stop: failed to deregeister kernel ctl!\n");
    }
    traceexec_kctlref = 0;
  }

  if (mac_policy_unregister(traceexec_policy_handle) != KERN_SUCCESS) {
    panic("traceexec_stop: failed to deregister MAC policy!\n");
  }
  traceexec_policy_handle = 0;

  return KERN_SUCCESS;
}
