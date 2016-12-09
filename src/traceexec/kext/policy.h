/**
  @brief Check whether BPF can read from a network interface
  @param bpf_d Subject; the BPF descriptor
  @param bpflabel Policy label for bpf_d 
  @param ifp Object; the network interface 
  @param ifnetlabel Policy label for ifp

  Determine whether the MAC framework should permit datagrams from
  the passed network interface to be delivered to the buffers of
  the passed BPF descriptor.  Return (0) for success, or an errno
  value for failure.  Suggested failure: EACCES for label mismatches,
  EPERM for lack of privilege.
*/
typedef int mpo_bpfdesc_check_receive_t(
  struct bpf_d *bpf_d,
  struct label *bpflabel,
  struct ifnet *ifp,
  struct label *ifnetlabel
);
/**
  @brief Indicate desire to change the process label at exec time
  @param old Existing subject credential
  @param vp File being executed
  @param offset Offset of binary within file being executed
  @param scriptvp Script being executed by interpreter, if any.
  @param vnodelabel Label corresponding to vp
  @param scriptvnodelabel Script vnode label
  @param execlabel Userspace provided execution label
  @param proc Object process
  @param macpolicyattr MAC policy-specific spawn attribute data
  @param macpolicyattrlen Length of policy-specific spawn attribute data
  @see mac_execve
  @see mpo_cred_label_update_execve_t
  @see mpo_vnode_check_exec_t

  Indicate whether this policy intends to update the label of a newly
  created credential from the existing subject credential (old).  This
  call occurs when a process executes the passed vnode.  If a policy
  returns success from this entry point, the mpo_cred_label_update_execve
  entry point will later be called with the same parameters.  Access
  has already been checked via the mpo_vnode_check_exec entry point,
  this entry point is necessary to preserve kernel locking constraints
  during program execution.

  The supplied vnode and vnodelabel correspond with the file actually
  being executed; in the case that the file is interpreted (for
  example, a script), the label of the original exec-time vnode has
  been preserved in scriptvnodelabel.

  The final label, execlabel, corresponds to a label supplied by a
  user space application through the use of the mac_execve system call.

  The vnode lock is held during this operation.  No changes should be
  made to the old credential structure.

  @warning Even if a policy returns 0, it should behave correctly in
  the presence of an invocation of mpo_cred_label_update_execve, as that
  call may happen as a result of another policy requesting a transition.

  @return Non-zero if a transition is required, 0 otherwise.
*/
typedef int mpo_cred_check_label_update_execve_t(
  kauth_cred_t old,
  struct vnode *vp,
  off_t offset,
  struct vnode *scriptvp,
  struct label *vnodelabel,
  struct label *scriptvnodelabel,
  struct label *execlabel,
  struct proc *p,
  void *macpolicyattr,
  size_t macpolicyattrlen
);
/**
  @brief Access control check for relabelling processes
  @param cred Subject credential
  @param newlabel New label to apply to the user credential
  @see mpo_cred_label_update_t
  @see mac_set_proc

  Determine whether the subject identified by the credential can relabel
  itself to the supplied new label (newlabel).  This access control check
  is called when the mac_set_proc system call is invoked.  A user space
  application will supply a new value, the value will be internalized
  and provided in newlabel.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_cred_check_label_update_t(
  kauth_cred_t cred,
  struct label *newlabel
);
/**
  @brief Create a credential label
  @param parent_cred Parent credential
  @param child_cred Child credential

  Set the label of a newly created credential, most likely using the
  information in the supplied parent credential.

  @warning This call is made when crcopy or crdup is invoked on a
  newly created struct ucred, and should not be confused with a
  process fork or creation event.
*/
typedef void mpo_cred_label_associate_t(
  kauth_cred_t parent_cred,
  kauth_cred_t child_cred
);
/**
  @brief Update credential at exec time
  @param old_cred Existing subject credential
  @param new_cred New subject credential to be labeled
  @param p Object process.
  @param vp File being executed
  @param offset Offset of binary within file being executed
  @param scriptvp Script being executed by interpreter, if any.
  @param vnodelabel Label corresponding to vp
  @param scriptvnodelabel Script vnode label
  @param execlabel Userspace provided execution label
  @param csflags Code signing flags to be set after exec
  @param macpolicyattr MAC policy-specific spawn attribute data.
  @param macpolicyattrlen Length of policy-specific spawn attribute data.
  @see mac_execve
  @see mpo_cred_check_label_update_execve_t
  @see mpo_vnode_check_exec_t

  Update the label of a newly created credential (new) from the
  existing subject credential (old).  This call occurs when a process
  executes the passed vnode and one of the loaded policy modules has
  returned success from the mpo_cred_check_label_update_execve entry point.
  Access has already been checked via the mpo_vnode_check_exec entry
  point, this entry point is only used to update any policy state.

  The supplied vnode and vnodelabel correspond with the file actually
  being executed; in the case that the file is interpreted (for
  example, a script), the label of the original exec-time vnode has
  been preserved in scriptvnodelabel.

  The final label, execlabel, corresponds to a label supplied by a
  user space application through the use of the mac_execve system call.

  If non-NULL, the value pointed to by disjointp will be set to 0 to
  indicate that the old and new credentials are not disjoint, or 1 to
  indicate that they are.

  The vnode lock is held during this operation.  No changes should be
  made to the old credential structure.
  @return 0 on success, Otherwise, return non-zero if update results in
  termination of child.
*/
typedef int mpo_cred_label_update_execve_t(
  kauth_cred_t old_cred,
  kauth_cred_t new_cred,
  struct proc *p,
  struct vnode *vp,
  off_t offset,
  struct vnode *scriptvp,
  struct label *vnodelabel,
  struct label *scriptvnodelabel,
  struct label *execlabel,
  u_int *csflags,
  void *macpolicyattr,
  size_t macpolicyattrlen,
  int *disjointp
);
/**
  @brief Update a credential label
  @param cred The existing credential
  @param newlabel A new label to apply to the credential
  @see mpo_cred_check_label_update_t
  @see mac_set_proc

  Update the label on a user credential, using the supplied new label.
  This is called as a result of a process relabel operation.  Access
  control was already confirmed by mpo_cred_check_label_update.
*/
typedef void mpo_cred_label_update_t(
  kauth_cred_t cred,
  struct label *newlabel
);
/**
  @brief Create a new devfs device
  @param dev Major and minor numbers of special file
  @param de "inode" of new device file
  @param label Destination label
  @param fullpath Path relative to mount (e.g. /dev) of new device file

  This entry point labels a new devfs device. The label will likely be based
  on the path to the device, or the major and minor numbers.
  The policy should store an appropriate label into 'label'.
*/
typedef void mpo_devfs_label_associate_device_t(
  dev_t dev,
  struct devnode *de,
  struct label *label,
  const char *fullpath
);
/**
  @brief Create a new devfs directory
  @param dirname Name of new directory
  @param dirnamelen Length of 'dirname'
  @param de "inode" of new directory
  @param label Destination label
  @param fullpath Path relative to mount (e.g. /dev) of new directory

  This entry point labels a new devfs directory. The label will likely be
  based on the path of the new directory. The policy should store an appropriate
  label into 'label'. The devfs root directory is labelled in this way.
*/
typedef void mpo_devfs_label_associate_directory_t(
  const char *dirname,
  int dirnamelen,
  struct devnode *de,
  struct label *label,
  const char *fullpath
);
/**
  @brief Access control check for fcntl
  @param cred Subject credential
  @param fg Fileglob structure
  @param label Policy label for fg
  @param cmd Control operation to be performed; see fcntl(2)
  @param arg fcnt arguments; see fcntl(2)

  Determine whether the subject identified by the credential can perform
  the file control operation indicated by cmd.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_file_check_fcntl_t(
  kauth_cred_t cred,
  struct fileglob *fg,
  struct label *label,
  int cmd,
  user_long_t arg
);
/**
  @brief Access control check for file ioctl
  @param cred Subject credential
  @param fg Fileglob structure
  @param label Policy label for fg
  @param cmd The ioctl command; see ioctl(2)

  Determine whether the subject identified by the credential can perform
  the ioctl operation indicated by cmd.

  @warning Since ioctl data is opaque from the standpoint of the MAC
  framework, policies must exercise extreme care when implementing
  access control checks.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.

*/
typedef int mpo_file_check_ioctl_t(
  kauth_cred_t cred,
  struct fileglob *fg,
  struct label *label,
  unsigned int cmd
);
/**
  @brief Access control check for file locking
  @param cred Subject credential
  @param fg Fileglob structure
  @param label Policy label for fg
  @param op The lock operation (F_GETLK, F_SETLK, F_UNLK)
  @param fl The flock structure

  Determine whether the subject identified by the credential can perform
  the lock operation indicated by op and fl on the file represented by fg.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.

*/
typedef int mpo_file_check_lock_t(
  kauth_cred_t cred,
  struct fileglob *fg,
  struct label *label,
  int op,
  struct flock *fl
);
/**
  @brief Access control check for mapping a file
  @param cred Subject credential
  @param fg fileglob representing file to map
  @param label Policy label associated with vp
  @param prot mmap protections; see mmap(2)
  @param flags Type of mapped object; see mmap(2)
  @param maxprot Maximum rights

  Determine whether the subject identified by the credential should be
  allowed to map the file represented by fg with the protections specified
  in prot.  The maxprot field holds the maximum permissions on the new
  mapping, a combination of VM_PROT_READ, VM_PROT_WRITE, and VM_PROT_EXECUTE.
  To avoid overriding prior access control checks, a policy should only
  remove flags from maxprot.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned. Suggested failure: EACCES for label mismatch or
  EPERM for lack of privilege.
*/
typedef int mpo_file_check_mmap_t(
  kauth_cred_t cred,
  struct fileglob *fg,
  struct label *label,
  int prot,
  int flags,
  uint64_t file_pos,
  int *maxprot
);
/**
  @brief Downgrade the mmap protections
  @param cred Subject credential
  @param fg file to map
  @param label Policy label associated with vp
  @param prot mmap protections to be downgraded

  Downgrade the mmap protections based on the subject and object labels.
*/
typedef void mpo_file_check_mmap_downgrade_t(
  kauth_cred_t cred,
  struct fileglob *fg,
  struct label *label,
  int *prot
);
/**
  @brief Access control for receiving a file descriptor
  @param cred Subject credential
  @param fg Fileglob structure
  @param label Policy label for fg

  Determine whether the subject identified by the credential can
  receive the fileglob structure represented by fg.

  @return Return 0 if access if granted, otherwise an appropriate
  value for errno should be returned.
*/
typedef int mpo_file_check_receive_t(
  kauth_cred_t cred,
  struct fileglob *fg,
  struct label *label
);
/**
  @brief Access control check for delivering a packet to a socket
  @param inp inpcb the socket is associated with
  @param inplabel Label of the inpcb
  @param m The mbuf being received
  @param mbuflabel Label of the mbuf being received
  @param family Address family, AF_*
  @param type Type of socket, SOCK_{STREAM,DGRAM,RAW}

  Determine whether the mbuf with label mbuflabel may be received
  by the socket associated with inpcb that has the label inplabel.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_inpcb_check_deliver_t(
  struct inpcb *inp,
  struct label *inplabel,
  struct mbuf *m,
  struct label *mbuflabel,
  int family,
  int type
);
/**
  @brief Device hardware access control
  @param devtype Type of device connected
  @param properties XML-formatted property list
  @param proplen Length of the property list

  This is the MAC Framework device access control, which is called by the I/O
  Kit when a new device is connected to the system to determine whether that
  device should be trusted.  A list of properties associated with the device
  is passed as an XML-formatted string.  The routine should examine these
  properties to determine the trustworthiness of the device.  A return value
  of EPERM forces the device to be claimed by a special device driver that
  will prevent its operation.

  @warning This is an experimental interface and may change in the future.

  @return Return EPERM to indicate that the device is untrusted and should
  not be allowed to operate.  Return zero to indicate that the device is
  trusted and should be allowed to operate normally.

*/
typedef int mpo_iokit_check_device_t(
  char *devtype,
  struct mac_module_data *mdata
);
/**
  @brief Access control check for opening an I/O Kit device
  @param cred Subject credential
  @param device_path Device path
  @param user_client User client instance
  @param user_client_type User client type

  Determine whether the subject identified by the credential can open an
  I/O Kit device at the passed path of the passed user client class and
  type.

  @return Return 0 if access is granted, or an appropriate value for
  errno should be returned.
*/
typedef int mpo_iokit_check_open_t(
  kauth_cred_t cred,
  io_object_t user_client,
  unsigned int user_client_type
);
/**
  @brief Access control check for setting I/O Kit device properties
  @param cred Subject credential
  @param entry Target device
  @param properties Property list

  Determine whether the subject identified by the credential can set
  properties on an I/O Kit device.

  @return Return 0 if access is granted, or an appropriate value for
  errno should be returned.
*/
typedef int mpo_iokit_check_set_properties_t(
  kauth_cred_t cred,
  io_object_t entry,
  io_object_t properties
);
/**
  @brief Indicate desire to filter I/O Kit devices properties
  @param cred Subject credential
  @param entry Target device
  @see mpo_iokit_check_get_property_t

  Indicate whether this policy may restrict the subject credential
  from reading properties of the target device.
  If a policy returns success from this entry point, the
  mpo_iokit_check_get_property entry point will later be called
  for each property that the subject credential tries to read from
  the target device.

  This entry point is primarilly to optimize bulk property reads
  by skipping calls to the mpo_iokit_check_get_property entry point
  for credentials / devices no MAC policy is interested in.

  @warning Even if a policy returns 0, it should behave correctly in
  the presence of an invocation of mpo_iokit_check_get_property, as that
  call may happen as a result of another policy requesting a transition.

  @return Non-zero if a transition is required, 0 otherwise.
 */
typedef int mpo_iokit_check_filter_properties_t(
  kauth_cred_t cred,
  io_object_t entry
);
/**
  @brief Access control check for getting I/O Kit device properties
  @param cred Subject credential
  @param entry Target device
  @param name Property name 

  Determine whether the subject identified by the credential can get
  properties on an I/O Kit device.

  @return Return 0 if access is granted, or an appropriate value for
  errno.
*/
typedef int mpo_iokit_check_get_property_t(
  kauth_cred_t cred,
  io_object_t entry,
  const char *name
);
/**
  @brief Access control check for software HID control
  @param cred Subject credential

  Determine whether the subject identified by the credential can
  control the HID (Human Interface Device) subsystem, such as to
  post synthetic keypresses, pointer movement and clicks.

  @return Return 0 if access is granted, or an appropriate value for
  errno.
*/
typedef int mpo_iokit_check_hid_control_t(
  kauth_cred_t cred
);
/**
  @brief Access control check for fsctl
  @param cred Subject credential
  @param mp The mount point
  @param label Label associated with the mount point
  @param com Filesystem-dependent request code; see fsctl(2)

  Determine whether the subject identified by the credential can perform
  the volume operation indicated by com.

  @warning The fsctl() system call is directly analogous to ioctl(); since
  the associated data is opaque from the standpoint of the MAC framework
  and since these operations can affect many aspects of system operation,
  policies must exercise extreme care when implementing access control checks.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_mount_check_fsctl_t(
  kauth_cred_t cred,
  struct mount *mp,
  struct label *label,
  unsigned int cmd
);
/**
  @brief Access control check for the retrieval of file system attributes
  @param cred Subject credential
  @param mp The mount structure of the file system
  @param vfa The attributes requested

  This entry point determines whether given subject can get information
  about the given file system.  This check happens during statfs() syscalls,
  but is also used by other parts within the kernel such as the audit system.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/

typedef int mpo_mount_check_getattr_t(
  kauth_cred_t cred,
  struct mount *mp,
  struct label *mp_label,
  struct vfs_attr *vfa
);
/**
  @brief Access control check for mounting a file system
  @param cred Subject credential
  @param vp Vnode that is to be the mount point
  @param vlabel Label associated with the vnode
  @param cnp Component name for vp
  @param vfc_name Filesystem type name

  Determine whether the subject identified by the credential can perform
  the mount operation on the target vnode.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_mount_check_mount_t(
  kauth_cred_t cred,
  struct vnode *vp,
  struct label *vlabel,
  struct componentname *cnp,
  const char *vfc_name
);
/**
  @brief Access control check remounting a filesystem
  @param cred Subject credential
  @param mp The mount point
  @param mlabel Label currently associated with the mount point

  Determine whether the subject identified by the credential can perform
  the remount operation on the target vnode.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_mount_check_remount_t(
  kauth_cred_t cred,
  struct mount *mp,
  struct label *mlabel
);
/**
  @brief Access control check for the settting of file system attributes
  @param cred Subject credential
  @param mp The mount structure of the file system
  @param vfa The attributes requested

  This entry point determines whether given subject can set information
  about the given file system, for example the volume name.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/

typedef int mpo_mount_check_setattr_t(
  kauth_cred_t cred,
  struct mount *mp,
  struct label *mp_label,
  struct vfs_attr *vfa
);
/**
  @brief Access control check for file system statistics
  @param cred Subject credential
  @param mp Object file system mount
  @param mntlabel Policy label for mp

  Determine whether the subject identified by the credential can see
  the results of a statfs performed on the file system. This call may
  be made in a number of situations, including during invocations of
  statfs(2) and related calls, as well as to determine what file systems
  to exclude from listings of file systems, such as when getfsstat(2)
  is invoked.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned. Suggested failure: EACCES for label mismatch
  or EPERM for lack of privilege.
*/
typedef int mpo_mount_check_stat_t(
  kauth_cred_t cred,
  struct mount *mp,
  struct label *mntlabel
);
/**
  @brief Access control check for unmounting a filesystem
  @param cred Subject credential
  @param mp The mount point
  @param mlabel Label associated with the mount point

  Determine whether the subject identified by the credential can perform
  the unmount operation on the target vnode.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_mount_check_umount_t(
  kauth_cred_t cred,
  struct mount *mp,
  struct label *mlabel
);
/**
  @brief Access control check for pipe ioctl
  @param cred Subject credential
  @param cpipe Object to be accessed
  @param pipelabel The label on the pipe
  @param cmd The ioctl command; see ioctl(2)

  Determine whether the subject identified by the credential can perform
  the ioctl operation indicated by cmd.

  @warning Since ioctl data is opaque from the standpoint of the MAC
  framework, policies must exercise extreme care when implementing
  access control checks.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.

*/
typedef int mpo_pipe_check_ioctl_t(
  kauth_cred_t cred,
  struct pipe *cpipe,
  struct label *pipelabel,
  unsigned int cmd
);
/**
  @brief Access control check for pipe kqfilter
  @param cred Subject credential
  @param kn Object knote
  @param cpipe Object to be accessed
  @param pipelabel Policy label for the pipe

  Determine whether the subject identified by the credential can
  receive the knote on the passed pipe.

  @return Return 0 if access if granted, otherwise an appropriate
  value for errno should be returned.
*/
typedef int mpo_pipe_check_kqfilter_t(
  kauth_cred_t cred,
  struct knote *kn,
  struct pipe *cpipe,
  struct label *pipelabel
);
/**
  @brief Access control check for POSIX semaphore create
  @param cred Subject credential
  @param name String name of the semaphore

  Determine whether the subject identified by the credential can create
  a POSIX semaphore specified by name.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_posixsem_check_create_t(
  kauth_cred_t cred,
  const char *name
);
/**
  @brief Access control check for POSIX semaphore open
  @param cred Subject credential
  @param ps Pointer to semaphore information structure
  @param semlabel Label associated with the semaphore

  Determine whether the subject identified by the credential can open
  the named POSIX semaphore with label semlabel.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_posixsem_check_open_t(
  kauth_cred_t cred,
  struct pseminfo *ps,
  struct label *semlabel
);
/**
  @brief Access control check for POSIX semaphore post
  @param cred Subject credential
  @param ps Pointer to semaphore information structure
  @param semlabel Label associated with the semaphore

  Determine whether the subject identified by the credential can unlock
  the named POSIX semaphore with label semlabel.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_posixsem_check_post_t(
  kauth_cred_t cred,
  struct pseminfo *ps,
  struct label *semlabel
);
/**
  @brief Access control check for POSIX semaphore unlink
  @param cred Subject credential
  @param ps Pointer to semaphore information structure
  @param semlabel Label associated with the semaphore
  @param name String name of the semaphore

  Determine whether the subject identified by the credential can remove
  the named POSIX semaphore with label semlabel.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_posixsem_check_unlink_t(
  kauth_cred_t cred,
  struct pseminfo *ps,
  struct label *semlabel,
  const char *name
);
/**
  @brief Access control check for POSIX semaphore wait
  @param cred Subject credential
  @param ps Pointer to semaphore information structure
  @param semlabel Label associated with the semaphore

  Determine whether the subject identified by the credential can lock
  the named POSIX semaphore with label semlabel.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_posixsem_check_wait_t(
  kauth_cred_t cred,
  struct pseminfo *ps,
  struct label *semlabel
);
/**
  @brief Access control check for POSIX shared memory region create
  @param cred Subject credential
  @param name String name of the shared memory region

  Determine whether the subject identified by the credential can create
  the POSIX shared memory region referenced by name.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_posixshm_check_create_t(
  kauth_cred_t cred,
  const char *name
);
/**
  @brief Access control check for mapping POSIX shared memory
  @param cred Subject credential
  @param ps Pointer to shared memory information structure
  @param shmlabel Label associated with the shared memory region
  @param prot mmap protections; see mmap(2)
  @param flags shmat flags; see shmat(2)

  Determine whether the subject identified by the credential can map
  the POSIX shared memory segment associated with shmlabel.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_posixshm_check_mmap_t(
  kauth_cred_t cred,
  struct pshminfo *ps,
  struct label *shmlabel,
  int prot,
  int flags
);
/**
  @brief Access control check for POSIX shared memory region open
  @param cred Subject credential
  @param ps Pointer to shared memory information structure
  @param shmlabel Label associated with the shared memory region
  @param fflags shm_open(2) open flags ('fflags' encoded)

  Determine whether the subject identified by the credential can open
  the POSIX shared memory region.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_posixshm_check_open_t(
  kauth_cred_t cred,
  struct pshminfo *ps,
  struct label *shmlabel,
  int fflags
);
/**
  @brief Access control check for POSIX shared memory stat
  @param cred Subject credential
  @param ps Pointer to shared memory information structure
  @param shmlabel Label associated with the shared memory region

  Determine whether the subject identified by the credential can obtain
  status for the POSIX shared memory segment associated with shmlabel.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_posixshm_check_stat_t(
  kauth_cred_t cred,
  struct pshminfo *ps,
  struct label *shmlabel
);
/**
  @brief Access control check for POSIX shared memory truncate
  @param cred Subject credential
  @param ps Pointer to shared memory information structure
  @param shmlabel Label associated with the shared memory region
  @param len Length to truncate or extend shared memory segment

  Determine whether the subject identified by the credential can truncate
  or extend (to len) the POSIX shared memory segment associated with shmlabel.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_posixshm_check_truncate_t(
  kauth_cred_t cred,
  struct pshminfo *ps,
  struct label *shmlabel,
  off_t len
);
/**
  @brief Access control check for POSIX shared memory unlink
  @param cred Subject credential
  @param ps Pointer to shared memory information structure
  @param shmlabel Label associated with the shared memory region
  @param name String name of the shared memory region

  Determine whether the subject identified by the credential can delete
  the POSIX shared memory segment associated with shmlabel.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_posixshm_check_unlink_t(
  kauth_cred_t cred,
  struct pshminfo *ps,
  struct label *shmlabel,
  const char *name
);
/**
 @brief Access control check for privileged operations
 @param cred Subject credential
 @param priv Requested privilege (see sys/priv.h)

 Determine whether the subject identified by the credential can perform
 a privileged operation.  Privileged operations are allowed if the cred
 is the superuser or any policy returns zero for mpo_priv_grant, unless
 any policy returns nonzero for mpo_priv_check.

 @return Return 0 if access is granted, otherwise EPERM should be returned.
*/
typedef int mpo_priv_check_t(
  kauth_cred_t cred,
  int priv
);
/**
  @brief Access control check for debugging process
  @param cred Subject credential
  @param proc Object process

  Determine whether the subject identified by the credential can debug
  the passed process. This call may be made in a number of situations,
  including use of the ptrace(2) and ktrace(2) APIs, as well as for some
  types of procfs operations.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned. Suggested failure: EACCES for label mismatch,
  EPERM for lack of privilege, or ESRCH to hide visibility of the target.
*/
typedef int mpo_proc_check_debug_t(
  kauth_cred_t cred,
  struct proc *proc
);
/**
  @brief Access control check for setting host special ports.
  @param cred Subject credential
  @param id The host special port to set
  @param port The new value to set for the special port

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_proc_check_set_host_special_port_t(
  kauth_cred_t cred,
  int id,
  struct ipc_port *port
);
/**
  @brief Access control check for setting host exception ports.
  @param cred Subject credential
  @param exceptions Exception port to set

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_proc_check_set_host_exception_port_t(
  kauth_cred_t cred,
  unsigned int exception
);
/**
  @brief Access control over pid_suspend and pid_resume
  @param cred Subject credential
  @param proc Subject process trying to run pid_suspend or pid_resume 
  @param sr Call is suspend (0) or resume (1)

  Determine whether the subject identified is allowed to suspend or resume
  other processes.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_proc_check_suspend_resume_t(
  kauth_cred_t cred,
  struct proc *proc,
  int sr
);
/**
  @brief Access control check for retrieving audit information
  @param cred Subject credential

  Determine whether the subject identified by the credential can get
  audit information such as the audit user ID, the preselection mask,
  the terminal ID and the audit session ID, using the getaudit() system call.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_proc_check_getaudit_t(
  kauth_cred_t cred
);
/**
  @brief Access control check for retrieving audit user ID
  @param cred Subject credential

  Determine whether the subject identified by the credential can get
  the user identity being used by the auditing system, using the getauid()
  system call.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_proc_check_getauid_t(
  kauth_cred_t cred
);
/**
  @brief Access control check for retrieving Login Context ID
  @param p0 Calling process
  @param p Effected process
  @param pid syscall PID argument

  Determine if getlcid(2) system call is permitted.

  Information returned by this system call is similar to that returned via
  process listings etc.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_proc_check_getlcid_t(
  struct proc *p0,
  struct proc *p,
  pid_t pid
);
/**
  @brief Access control check for retrieving ledger information
  @param cred Subject credential
  @param target Object process
  @param op ledger operation

  Determine if ledger(2) system call is permitted.

  Information returned by this system call is similar to that returned via
  process listings etc.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_proc_check_ledger_t(
  kauth_cred_t cred,
  struct proc *target,
  int op
);
/**
  @brief Access control check for escaping default CPU usage monitor parameters.
  @param cred Subject credential
  
  Determine if a credential has permission to program CPU usage monitor parameters
  that are less restrictive than the global system-wide defaults.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_proc_check_cpumon_t(
  kauth_cred_t cred
);
/**
  @brief Access control check for retrieving process information.
  @param cred Subject credential
  @param target Target process (may be null, may be zombie)

  Determine if a credential has permission to access process information as defined
  by call number and flavor on target process

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_proc_check_proc_info_t(
  kauth_cred_t cred,
  struct proc *target,
  int callnum,
  int flavor
);
/**
  @brief Access control check for mmap MAP_ANON
  @param proc User process requesting the memory
  @param cred Subject credential
  @param u_addr Start address of the memory range
  @param u_size Length address of the memory range
  @param prot mmap protections; see mmap(2)
  @param flags Type of mapped object; see mmap(2)
  @param maxprot Maximum rights

  Determine whether the subject identified by the credential should be
  allowed to obtain anonymous memory using the specified flags and 
  protections on the new mapping. MAP_ANON will always be present in the
  flags. Certain combinations of flags with a non-NULL addr may
  cause a mapping to be rejected before this hook is called. The maxprot field
  holds the maximum permissions on the new mapping, a combination of
  VM_PROT_READ, VM_PROT_WRITE and VM_PROT_EXECUTE. To avoid overriding prior
  access control checks, a policy should only remove flags from maxprot.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned. Suggested failure: EPERM for lack of privilege.
*/
typedef int mpo_proc_check_map_anon_t(
  struct proc *proc,
  kauth_cred_t cred,
  user_addr_t u_addr,
  user_size_t u_size,
  int prot,
  int flags,
  int *maxprot
);
/**
  @brief Access control check for setting audit information
  @param cred Subject credential
  @param ai Audit information

  Determine whether the subject identified by the credential can set
  audit information such as the the preselection mask, the terminal ID
  and the audit session ID, using the setaudit() system call.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_proc_check_setaudit_t(
  kauth_cred_t cred,
  struct auditinfo_addr *ai
);
/**
  @brief Access control check for setting audit user ID
  @param cred Subject credential
  @param auid Audit user ID

  Determine whether the subject identified by the credential can set
  the user identity used by the auditing system, using the setauid()
  system call.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_proc_check_setauid_t(
  kauth_cred_t cred,
  uid_t auid
);
/**
  @brief Access control check for setting the Login Context
  @param p0 Calling process
  @param p Effected process
  @param pid syscall PID argument
  @param lcid syscall LCID argument

  Determine if setlcid(2) system call is permitted.

  See xnu/bsd/kern/kern_prot.c:setlcid() implementation for example of
  decoding syscall arguments to determine action desired by caller.

  Five distinct actions are possible: CREATE JOIN LEAVE ADOPT ORPHAN

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_proc_check_setlcid_t(
  struct proc *p0,
  struct proc *p,
  pid_t pid,
  pid_t lcid
);
/**
  @brief Access control check for delivering signal
  @param cred Subject credential
  @param proc Object process
  @param signum Signal number; see kill(2)

  Determine whether the subject identified by the credential can deliver
  the passed signal to the passed process.

  @warning Programs typically expect to be able to send and receive
  signals as part or their normal process lifecycle; caution should be
  exercised when implementing access controls over signal events.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned. Suggested failure: EACCES for label mismatch,
  EPERM for lack of privilege, or ESRCH to limit visibility.
*/
typedef int mpo_proc_check_signal_t(
  kauth_cred_t cred,
  struct proc *proc,
  int signum
);
/**
  @brief Destroy process label
  @param label The label to be destroyed

  Destroy a process label.  Since the object is going
  out of scope, policy modules should free any internal storage
  associated with the label so that it may be destroyed.
*/
typedef void mpo_proc_label_destroy_t(
  struct label *label
);
/**
  @brief Initialize process label
  @param label New label to initialize
  @see mpo_cred_label_init_t

  Initialize the label for a newly instantiated BSD process structure.
  Normally, security policies will store the process label in the user
  credential rather than here in the process structure.  However,
  there are some floating label policies that may need to temporarily
  store a label in the process structure until it is safe to update
  the user credential label.  Sleeping is permitted.
*/
typedef void mpo_proc_label_init_t(
  struct label *label
);
/**
  @brief Access control check for socket accept
  @param cred Subject credential
  @param socket Object socket
  @param socklabel Policy label for socket

  Determine whether the subject identified by the credential can accept()
  a new connection on the socket from the host specified by addr.

  @return Return 0 if access if granted, otherwise an appropriate
  value for errno should be returned.
*/
typedef int mpo_socket_check_accept_t(
  kauth_cred_t cred,
  socket_t so,
  struct label *socklabel
);
/**
  @brief Access control check for a pending socket accept
  @param cred Subject credential
  @param so Object socket
  @param socklabel Policy label for socket
  @param addr Address of the listening socket (coming soon)

  Determine whether the subject identified by the credential can accept()
  a pending connection on the socket from the host specified by addr.

  @return Return 0 if access if granted, otherwise an appropriate
  value for errno should be returned.
*/
typedef int mpo_socket_check_accepted_t(
  kauth_cred_t cred,
  socket_t so,
  struct label *socklabel,
  struct sockaddr *addr
);
/**
  @brief Access control check for socket bind
  @param cred Subject credential
  @param so Object socket
  @param socklabel Policy label for socket
  @param addr Name to assign to the socket

  Determine whether the subject identified by the credential can bind()
  the name (addr) to the socket.

  @return Return 0 if access if granted, otherwise an appropriate
  value for errno should be returned.
*/
typedef int mpo_socket_check_bind_t(
  kauth_cred_t cred,
  socket_t so,
  struct label *socklabel,
  struct sockaddr *addr
);
/**
  @brief Access control check for socket connect
  @param cred Subject credential
  @param so Object socket
  @param socklabel Policy label for socket
  @param addr Name to assign to the socket

  Determine whether the subject identified by the credential can
  connect() the passed socket to the remote host specified by addr.

  @return Return 0 if access if granted, otherwise an appropriate
  value for errno should be returned.
*/
typedef int mpo_socket_check_connect_t(
  kauth_cred_t cred,
  socket_t so,
  struct label *socklabel,
  struct sockaddr *addr
);
/**
  @brief Access control check for socket() system call.
  @param cred Subject credential
  @param domain communication domain
  @param type socket type
  @param protocol socket protocol

  Determine whether the subject identified by the credential can
  make the socket() call.

  @return Return 0 if access if granted, otherwise an appropriate
  value for errno should be returned.
*/
typedef int mpo_socket_check_create_t(
  kauth_cred_t cred,
  int domain,
  int type,
  int protocol
);
/**
  @brief Access control check for socket kqfilter
  @param cred Subject credential
  @param kn Object knote
  @param so Object socket
  @param socklabel Policy label for socket

  Determine whether the subject identified by the credential can
  receive the knote on the passed socket.

  @return Return 0 if access if granted, otherwise an appropriate
  value for errno should be returned.
*/
typedef int mpo_socket_check_kqfilter_t(
  kauth_cred_t cred,
  struct knote *kn,
  socket_t so,
  struct label *socklabel
);
/**
  @brief Access control check for socket listen
  @param cred Subject credential
  @param so Object socket
  @param socklabel Policy label for socket

  Determine whether the subject identified by the credential can
  listen() on the passed socket.

  @return Return 0 if access if granted, otherwise an appropriate
  value for errno should be returned.
*/
typedef int mpo_socket_check_listen_t(
  kauth_cred_t cred,
  socket_t so,
  struct label *socklabel
);
/**
  @brief Access control check for setting socket options
  @param cred Subject credential
  @param so Object socket
  @param socklabel Policy label for so
  @param sopt The options being set

  Determine whether the subject identified by the credential can
  execute the setsockopt system call on the given socket.

  @return Return 0 if access if granted, otherwise an appropriate
  value for errno should be returned.
*/
typedef int mpo_socket_check_setsockopt_t(
  kauth_cred_t cred,
  socket_t so,
  struct label *socklabel,
  struct sockopt *sopt
);
/**
  @brief Access control check for getting socket options
  @param cred Subject credential
  @param so Object socket
  @param socklabel Policy label for so
  @param sopt The options to get

  Determine whether the subject identified by the credential can
  execute the getsockopt system call on the given socket.

  @return Return 0 if access if granted, otherwise an appropriate
  value for errno should be returned.
*/
typedef int mpo_socket_check_getsockopt_t(
  kauth_cred_t cred,
  socket_t so,
  struct label *socklabel,
  struct sockopt *sopt
);
/**
  @brief Access control check for enabling accounting
  @param cred Subject credential
  @param vp Accounting file
  @param vlabel Label associated with vp

  Determine whether the subject should be allowed to enable accounting,
  based on its label and the label of the accounting log file.  See
  acct(5) for more information.

  As accounting is disabled by passing NULL to the acct(2) system call,
  the policy should be prepared for both 'vp' and 'vlabel' to be NULL.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_system_check_acct_t(
  kauth_cred_t cred,
  struct vnode *vp,
  struct label *vlabel
);
/**
  @brief Access control check for audit
  @param cred Subject credential
  @param record Audit record
  @param length Audit record length

  Determine whether the subject identified by the credential can submit
  an audit record for inclusion in the audit log via the audit() system call.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_system_check_audit_t(
  kauth_cred_t cred,
  void *record,
  int length
);
/**
  @brief Access control check for controlling audit
  @param cred Subject credential
  @param vp Audit file
  @param vl Label associated with vp

  Determine whether the subject should be allowed to enable auditing using
  the auditctl() system call, based on its label and the label of the proposed
  audit file.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_system_check_auditctl_t(
  kauth_cred_t cred,
  struct vnode *vp,
  struct label *vl
);
/**
  @brief Access control check for manipulating auditing
  @param cred Subject credential
  @param cmd Audit control command

  Determine whether the subject identified by the credential can perform
  the audit subsystem control operation cmd via the auditon() system call.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_system_check_auditon_t(
  kauth_cred_t cred,
  int cmd
);
/**
  @brief Access control check for using CHUD facilities
  @param cred Subject credential

  Determine whether the subject identified by the credential can perform
  performance-related tasks using the CHUD system call.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_system_check_chud_t(
  kauth_cred_t cred
);
/**
  @brief Access control check for obtaining the host control port
  @param cred Subject credential

  Determine whether the subject identified by the credential can
  obtain the host control port.

  @return Return 0 if access is granted, or non-zero otherwise.
*/
typedef int mpo_system_check_host_priv_t(
  kauth_cred_t cred
);
/**
  @brief Access control check for obtaining system information
  @param cred Subject credential
  @param info_type A description of the information requested

  Determine whether the subject identified by the credential should be
  allowed to obtain information about the system.

  This is a generic hook that can be used in a variety of situations where
  information is being returned that might be considered sensitive.
  Rather than adding a new MAC hook for every such interface, this hook can
  be called with a string identifying the type of information requested.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_system_check_info_t(
  kauth_cred_t cred,
  const char *info_type
);
/**
  @brief Access control check for calling NFS services
  @param cred Subject credential

  Determine whether the subject identified by the credential should be
  allowed to call nfssrv(2).

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_system_check_nfsd_t(
  kauth_cred_t cred
);
/**
  @brief Access control check for reboot
  @param cred Subject credential
  @param howto howto parameter from reboot(2)

  Determine whether the subject identified by the credential should be
  allowed to reboot the system in the specified manner.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_system_check_reboot_t(
  kauth_cred_t cred,
  int howto
);
/**
  @brief Access control check for setting system clock
  @param cred Subject credential

  Determine whether the subject identified by the credential should be
  allowed to set the system clock.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_system_check_settime_t(
  kauth_cred_t cred
);
/**
  @brief Access control check for removing swap devices
  @param cred Subject credential
  @param vp Swap device
  @param label Label associated with vp

  Determine whether the subject identified by the credential should be
  allowed to remove vp as a swap device.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_system_check_swapoff_t(
  kauth_cred_t cred,
  struct vnode *vp,
  struct label *label
);
/**
  @brief Access control check for adding swap devices
  @param cred Subject credential
  @param vp Swap device
  @param label Label associated with vp

  Determine whether the subject identified by the credential should be
  allowed to add vp as a swap device.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_system_check_swapon_t(
  kauth_cred_t cred,
  struct vnode *vp,
  struct label *label
);
/**
  @brief Access control check for sysctl
  @param cred Subject credential
  @param namestring String representation of sysctl name.
  @param name Integer name; see sysctl(3)
  @param namelen Length of name array of integers; see sysctl(3)
  @param old 0 or address where to store old value; see sysctl(3)
  @param oldlen Length of old buffer; see sysctl(3)
  @param newvalue 0 or address of new value; see sysctl(3)
  @param newlen Length of new buffer; see sysctl(3)

  Determine whether the subject identified by the credential should be
  allowed to make the specified sysctl(3) transaction.

  The sysctl(3) call specifies that if the old value is not desired,
  oldp and oldlenp should be set to NULL.  Likewise, if a new value is
  not to be set, newp should be set to NULL and newlen set to 0.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_system_check_sysctlbyname_t(
  kauth_cred_t cred,
  const char *namestring,
  int *name,
  u_int namelen,
  user_addr_t old,  /* NULLOK */
  size_t oldlen,
  user_addr_t newvalue, /* NULLOK */
  size_t newlen
);
/**
  @brief Access control check for kas_info
  @param cred Subject credential
  @param selector Category of information to return. See kas_info.h

  Determine whether the subject identified by the credential can perform
  introspection of the kernel address space layout for
  debugging/performance analysis.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_system_check_kas_info_t(
  kauth_cred_t cred,
  int selector
);
/**
  @brief Access control check for System V message enqueuing
  @param cred Subject credential
  @param msgptr The message
  @param msglabel The message's label
  @param msqkptr The message queue
  @param msqlabel The message queue's label

  Determine whether the subject identified by the credential can add the
  given message to the given message queue.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_sysvmsq_check_enqueue_t(
  kauth_cred_t cred,
  struct msg *msgptr,
  struct label *msglabel,
  struct msqid_kernel *msqptr,
  struct label *msqlabel
);
/**
  @brief Access control check for System V message reception
  @param cred The credential of the intended recipient
  @param msgptr The message
  @param msglabel The message's label

  Determine whether the subject identified by the credential can receive
  the given message.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_sysvmsq_check_msgrcv_t(
  kauth_cred_t cred,
  struct msg *msgptr,
  struct label *msglabel
);
/**
  @brief Access control check for System V message queue removal
  @param cred The credential of the caller
  @param msgptr The message
  @param msglabel The message's label

  System V message queues are removed using the msgctl() system call.
  The system will iterate over each messsage in the queue, calling this
  function for each, to determine whether the caller has the appropriate
  credentials.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_sysvmsq_check_msgrmid_t(
  kauth_cred_t cred,
  struct msg *msgptr,
  struct label *msglabel
);
/**
  @brief Access control check for msgctl()
  @param cred The credential of the caller
  @param msqptr The message queue
  @param msqlabel The message queue's label

  This access check is performed to validate calls to msgctl().

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_sysvmsq_check_msqctl_t(
  kauth_cred_t cred,
  struct msqid_kernel *msqptr,
  struct label *msqlabel,
  int cmd
);
/**
  @brief Access control check to get a System V message queue
  @param cred The credential of the caller
  @param msqptr The message queue requested
  @param msqlabel The message queue's label

  On a call to msgget(), if the queue requested already exists,
  and it is a public queue, this check will be performed before the
  queue's ID is returned to the user.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_sysvmsq_check_msqget_t(
  kauth_cred_t cred,
  struct msqid_kernel *msqptr,
  struct label *msqlabel
);
/**
  @brief Access control check to receive a System V message from the given queue
  @param cred The credential of the caller
  @param msqptr The message queue to receive from
  @param msqlabel The message queue's label

  On a call to msgrcv(), this check is performed to determine whether the
  caller has receive rights on the given queue.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_sysvmsq_check_msqrcv_t(
  kauth_cred_t cred,
  struct msqid_kernel *msqptr,
  struct label *msqlabel
);
/**
  @brief Access control check to send a System V message to the given queue
  @param cred The credential of the caller
  @param msqptr The message queue to send to
  @param msqlabel The message queue's label

  On a call to msgsnd(), this check is performed to determine whether the
  caller has send rights on the given queue.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_sysvmsq_check_msqsnd_t(
  kauth_cred_t cred,
  struct msqid_kernel *msqptr,
  struct label *msqlabel
);
/**
  @brief Access control check for System V semaphore control operation
  @param cred Subject credential
  @param semakptr Pointer to semaphore identifier
  @param semaklabel Label associated with semaphore
  @param cmd Control operation to be performed; see semctl(2)

  Determine whether the subject identified by the credential can perform
  the operation indicated by cmd on the System V semaphore semakptr.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_sysvsem_check_semctl_t(
  kauth_cred_t cred,
  struct semid_kernel *semakptr,
  struct label *semaklabel,
  int cmd
);
/**
  @brief Access control check for obtaining a System V semaphore
  @param cred Subject credential
  @param semakptr Pointer to semaphore identifier
  @param semaklabel Label to associate with the semaphore

  Determine whether the subject identified by the credential can
  obtain a System V semaphore.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_sysvsem_check_semget_t(
  kauth_cred_t cred,
  struct semid_kernel *semakptr,
  struct label *semaklabel
);
/**
  @brief Access control check for System V semaphore operations
  @param cred Subject credential
  @param semakptr Pointer to semaphore identifier
  @param semaklabel Label associated with the semaphore
  @param accesstype Flags to indicate access (read and/or write)

  Determine whether the subject identified by the credential can
  perform the operations on the System V semaphore indicated by
  semakptr.  The accesstype flags hold the maximum set of permissions
  from the sem_op array passed to the semop system call.  It may
  contain SEM_R for read-only operations or SEM_A for read/write
  operations.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_sysvsem_check_semop_t(
  kauth_cred_t cred,
  struct semid_kernel *semakptr,
  struct label *semaklabel,
  size_t accesstype
);
/**
  @brief Access control check for mapping System V shared memory
  @param cred Subject credential
  @param shmsegptr Pointer to shared memory segment identifier
  @param shmseglabel Label associated with the shared memory segment
  @param shmflg shmat flags; see shmat(2)

  Determine whether the subject identified by the credential can map
  the System V shared memory segment associated with shmsegptr.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_sysvshm_check_shmat_t(
  kauth_cred_t cred,
  struct shmid_kernel *shmsegptr,
  struct label *shmseglabel,
  int shmflg
);
/**
  @brief Access control check for System V shared memory control operation
  @param cred Subject credential
  @param shmsegptr Pointer to shared memory segment identifier
  @param shmseglabel Label associated with the shared memory segment
  @param cmd Control operation to be performed; see shmctl(2)

  Determine whether the subject identified by the credential can perform
  the operation indicated by cmd on the System V shared memory segment
  shmsegptr.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_sysvshm_check_shmctl_t(
  kauth_cred_t cred,
  struct shmid_kernel *shmsegptr,
  struct label *shmseglabel,
  int cmd
);
/**
  @brief Access control check for unmapping System V shared memory
  @param cred Subject credential
  @param shmsegptr Pointer to shared memory segment identifier
  @param shmseglabel Label associated with the shared memory segment

  Determine whether the subject identified by the credential can unmap
  the System V shared memory segment associated with shmsegptr.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_sysvshm_check_shmdt_t(
  kauth_cred_t cred,
  struct shmid_kernel *shmsegptr,
  struct label *shmseglabel
);
/**
  @brief Access control check obtaining System V shared memory identifier
  @param cred Subject credential
  @param shmsegptr Pointer to shared memory segment identifier
  @param shmseglabel Label associated with the shared memory segment
  @param shmflg shmget flags; see shmget(2)

  Determine whether the subject identified by the credential can get
  the System V shared memory segment address.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_sysvshm_check_shmget_t(
  kauth_cred_t cred,
  struct shmid_kernel *shmsegptr,
  struct label *shmseglabel,
  int shmflg
);
/**
  @brief Create a System V shared memory region label
  @param cred Subject credential
  @param shmsegptr The shared memory region being created
  @param shmlabel Label to associate with the new shared memory region

  Label a new System V shared memory region.  The label was previously
  initialized and associated with the shared memory region.  At this
  time, an appropriate initial label value should be assigned to the
  object and stored in shmlabel.
*/
typedef void mpo_sysvshm_label_associate_t(
  kauth_cred_t cred,
  struct shmid_kernel *shmsegptr,
  struct label *shmlabel
);
/**
  @brief Access control check for getting a process's task name
  @param cred Subject credential
  @param proc Object process

  Determine whether the subject identified by the credential can get
  the passed process's task name port.
  This call is used by the task_name_for_pid(2) API.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned. Suggested failure: EACCES for label mismatch,
  EPERM for lack of privilege, or ESRCH to hide visibility of the target.
*/
typedef int mpo_proc_check_get_task_name_t(
  kauth_cred_t cred,
  struct proc *p
);
/**
  @brief Access control check for getting a process's task port
  @param cred Subject credential
  @param proc Object process

  Determine whether the subject identified by the credential can get
  the passed process's task control port.
  This call is used by the task_for_pid(2) API.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned. Suggested failure: EACCES for label mismatch,
  EPERM for lack of privilege, or ESRCH to hide visibility of the target.
*/
typedef int mpo_proc_check_get_task_t(
  kauth_cred_t cred,
  struct proc *p
);

/**
  @brief Access control check for exposing a process's task port
  @param cred Subject credential
  @param proc Object process

  Determine whether the subject identified by the credential can expose
  the passed process's task control port.
  This call is used by the accessor APIs like processor_set_tasks() and
  processor_set_threads().

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned. Suggested failure: EACCES for label mismatch,
  EPERM for lack of privilege, or ESRCH to hide visibility of the target.
*/
typedef int mpo_proc_check_expose_task_t(
  kauth_cred_t cred,
  struct proc *p
);

/**
 @brief Check whether task's IPC may inherit across process exec
 @param proc current process instance
 @param cur_vp vnode pointer to current instance
 @param cur_offset offset of binary of currently executing image
 @param img_vp vnode pointer to to be exec'ed image
 @param img_offset offset into file which is selected for execution
 @param scriptvp vnode pointer of script file if any.
 @return Return 0 if access is granted.
  EPERM     if parent does not have any entitlements.
  EACCESS   if mismatch in entitlements
*/
typedef int mpo_proc_check_inherit_ipc_ports_t(
  struct proc *p,
  struct vnode *cur_vp,
  off_t cur_offset,
  struct vnode *img_vp,
  off_t img_offset,
  struct vnode *scriptvp
);

/**
 @brief Privilege check for a process to run invalid
 @param proc Object process
 
 Determine whether the process may execute even though the system determined
 that it is untrusted (eg unidentified / modified code).
 
 @return Return 0 if access is granted, otherwise an appropriate value for
 errno should be returned.
 */
typedef int mpo_proc_check_run_cs_invalid_t(
  struct proc *p
);

/**
  @brief Check vnode access
  @param cred Subject credential
  @param vp Object vnode
  @param label Label for vp
  @param acc_mode access(2) flags

  Determine how invocations of access(2) and related calls by the
  subject identified by the credential should return when performed
  on the passed vnode using the passed access flags. This should
  generally be implemented using the same semantics used in
  mpo_vnode_check_open.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned. Suggested failure: EACCES for label mismatch or
  EPERM for lack of privilege.
*/
typedef int mpo_vnode_check_access_t(
  kauth_cred_t cred,
  struct vnode *vp,
  struct label *label,
  int acc_mode
);
/**
  @brief Access control check for changing root directory
  @param cred Subject credential
  @param dvp Directory vnode
  @param dlabel Policy label associated with dvp
  @param cnp Component name for dvp

  Determine whether the subject identified by the credential should be
  allowed to chroot(2) into the specified directory (dvp).

  @return In the event of an error, an appropriate value for errno
  should be returned, otherwise return 0 upon success.
*/
typedef int mpo_vnode_check_chroot_t(
  kauth_cred_t cred,
  struct vnode *dvp,
  struct label *dlabel,
  struct componentname *cnp
);
/**
  @brief Access control check for creating vnode
  @param cred Subject credential
  @param dvp Directory vnode
  @param dlabel Policy label for dvp
  @param cnp Component name for dvp
  @param vap vnode attributes for vap

  Determine whether the subject identified by the credential can create
  a vnode with the passed parent directory, passed name information,
  and passed attribute information. This call may be made in a number of
  situations, including as a result of calls to open(2) with O_CREAT,
  mknod(2), mkfifo(2), and others.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned. Suggested failure: EACCES for label mismatch or
  EPERM for lack of privilege.
*/
typedef int mpo_vnode_check_create_t(
  kauth_cred_t cred,
  struct vnode *dvp,
  struct label *dlabel,
  struct componentname *cnp,
  struct vnode_attr *vap
);
/**
  @brief Access control check for deleting extended attribute
  @param cred Subject credential
  @param vp Object vnode
  @param vlabel Label associated with vp
  @param name Extended attribute name

  Determine whether the subject identified by the credential can delete
  the extended attribute from the passed vnode.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned. Suggested failure: EACCES for label mismatch or
  EPERM for lack of privilege.
*/
typedef int mpo_vnode_check_deleteextattr_t(
  kauth_cred_t cred,
  struct vnode *vp,
  struct label *vlabel,
  const char *name
);
/**
  @brief Access control check for exchanging file data
  @param cred Subject credential
  @param v1 vnode 1 to swap
  @param vl1 Policy label for v1
  @param v2 vnode 2 to swap
  @param vl2 Policy label for v2

  Determine whether the subject identified by the credential can swap the data
  in the two supplied vnodes.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned. Suggested failure: EACCES for label mismatch or
  EPERM for lack of privilege.
*/
typedef int mpo_vnode_check_exchangedata_t(
  kauth_cred_t cred,
  struct vnode *v1,
  struct label *vl1,
  struct vnode *v2,
  struct label *vl2
);
/**
  @brief Access control check for executing the vnode
  @param cred Subject credential
  @param vp Object vnode to execute
  @param scriptvp Script being executed by interpreter, if any.
  @param vnodelabel Label corresponding to vp
  @param scriptvnodelabel Script vnode label
  @param execlabel Userspace provided execution label
  @param cnp Component name for file being executed
  @param macpolicyattr MAC policy-specific spawn attribute data.
  @param macpolicyattrlen Length of policy-specific spawn attribute data.

  Determine whether the subject identified by the credential can execute
  the passed vnode. Determination of execute privilege is made separately
  from decisions about any process label transitioning event.

  The final label, execlabel, corresponds to a label supplied by a
  user space application through the use of the mac_execve system call.
  This label will be NULL if the user application uses the the vendor
  execve(2) call instead of the MAC Framework mac_execve() call.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned. Suggested failure: EACCES for label mismatch or
  EPERM for lack of privilege.
*/
typedef int mpo_vnode_check_exec_t(
  kauth_cred_t cred,
  struct vnode *vp,
  struct vnode *scriptvp,
  struct label *vnodelabel,
  struct label *scriptlabel,
  struct label *execlabel,  /* NULLOK */
  struct componentname *cnp,
  u_int *csflags,
  void *macpolicyattr,
  size_t macpolicyattrlen
);
/**
  @brief Access control check after determining the code directory hash
 */
typedef int mpo_vnode_check_signature_t(struct vnode *vp,  struct label *label, 
          off_t macho_offset, unsigned char *sha1, 
          const void *signature, int size,
          int flags, int *is_platform_binary);

/**
  @brief Access control check for retrieving file attributes
  @param cred Subject credential
  @param vp Object vnode
  @param vlabel Policy label for vp
  @param alist List of attributes to retrieve

  Determine whether the subject identified by the credential can read
  various attributes of the specified vnode, or the filesystem or volume on
  which that vnode resides. See <sys/attr.h> for definitions of the
  attributes.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned. Suggested failure: EACCES for label mismatch or
  EPERM for lack of privilege. Access control covers all attributes requested
  with this call; the security policy is not permitted to change the set of
  attributes requested.
*/
typedef int mpo_vnode_check_getattrlist_t(
  kauth_cred_t cred,
  struct vnode *vp,
  struct label *vlabel,
  struct attrlist *alist
);
/**
  @brief Access control check for retrieving an extended attribute
  @param cred Subject credential
  @param vp Object vnode
  @param label Policy label for vp
  @param name Extended attribute name
  @param uio I/O structure pointer

  Determine whether the subject identified by the credential can retrieve
  the extended attribute from the passed vnode.  The uio parameter
  will be NULL when the getxattr(2) call has been made with a NULL data
  value; this is done to request the size of the data only.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned. Suggested failure: EACCES for label mismatch or
  EPERM for lack of privilege.
*/
typedef int mpo_vnode_check_getextattr_t(
  kauth_cred_t cred,
  struct vnode *vp,
  struct label *label,    /* NULLOK */
  const char *name,
  struct uio *uio     /* NULLOK */
);
/**
  @brief Access control check for ioctl
  @param cred Subject credential
  @param vp Object vnode
  @param label Policy label for vp
  @param com Device-dependent request code; see ioctl(2)

  Determine whether the subject identified by the credential can perform
  the ioctl operation indicated by com.

  @warning Since ioctl data is opaque from the standpoint of the MAC
  framework, and since ioctls can affect many aspects of system
  operation, policies must exercise extreme care when implementing
  access control checks.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_vnode_check_ioctl_t(
  kauth_cred_t cred,
  struct vnode *vp,
  struct label *label,
  unsigned int cmd
);
/**
  @brief Access control check for vnode kqfilter
  @param cred Subject credential
  @param kn Object knote
  @param vp Object vnode
  @param label Policy label for vp

  Determine whether the subject identified by the credential can
  receive the knote on the passed vnode.

  @return Return 0 if access if granted, otherwise an appropriate
  value for errno should be returned.
*/
typedef int mpo_vnode_check_kqfilter_t(
  kauth_cred_t active_cred,
  kauth_cred_t file_cred,   /* NULLOK */
  struct knote *kn,
  struct vnode *vp,
  struct label *label
);
/**
  @brief Access control check for creating link
  @param cred Subject credential
  @param dvp Directory vnode
  @param dlabel Policy label associated with dvp
  @param vp Link destination vnode
  @param label Policy label associated with vp
  @param cnp Component name for the link being created

  Determine whether the subject identified by the credential should be
  allowed to create a link to the vnode vp with the name specified by cnp.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_vnode_check_link_t(
  kauth_cred_t cred,
  struct vnode *dvp,
  struct label *dlabel,
  struct vnode *vp,
  struct label *label,
  struct componentname *cnp
);
/**
  @brief Access control check for listing extended attributes
  @param cred Subject credential
  @param vp Object vnode
  @param vlabel Policy label associated with vp

  Determine whether the subject identified by the credential can retrieve
  a list of named extended attributes from a vnode.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_vnode_check_listextattr_t(
  kauth_cred_t cred,
  struct vnode *vp,
  struct label *vlabel
);
/**
  @brief Access control check for lookup
  @param cred Subject credential
  @param dvp Object vnode
  @param dlabel Policy label for dvp
  @param cnp Component name being looked up

  Determine whether the subject identified by the credential can perform
  a lookup in the passed directory vnode for the passed name (cnp).

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned. Suggested failure: EACCES for label mismatch or
  EPERM for lack of privilege.
*/
typedef int mpo_vnode_check_lookup_t(
  kauth_cred_t cred,
  struct vnode *dvp,
  struct label *dlabel,
  struct componentname *cnp
);
/**
  @brief Access control check for open
  @param cred Subject credential
  @param vp Object vnode
  @param label Policy label associated with vp
  @param acc_mode open(2) access mode

  Determine whether the subject identified by the credential can perform
  an open operation on the passed vnode with the passed access mode.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned. Suggested failure: EACCES for label mismatch or
  EPERM for lack of privilege.
*/
typedef int mpo_vnode_check_open_t(
  kauth_cred_t cred,
  struct vnode *vp,
  struct label *label,
  int acc_mode
);
/**
  @brief Access control check for read directory
  @param cred Subject credential
  @param dvp Object directory vnode
  @param dlabel Policy label for dvp

  Determine whether the subject identified by the credential can
  perform a readdir operation on the passed directory vnode.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned. Suggested failure: EACCES for label mismatch or
  EPERM for lack of privilege.
*/
typedef int mpo_vnode_check_readdir_t(
  kauth_cred_t cred,    /* SUBJECT */
  struct vnode *dvp,    /* OBJECT */
  struct label *dlabel    /* LABEL */
);
/**
  @brief Access control check for read link
  @param cred Subject credential
  @param vp Object vnode
  @param label Policy label for vp

  Determine whether the subject identified by the credential can perform
  a readlink operation on the passed symlink vnode.  This call can be made
  in a number of situations, including an explicit readlink call by the
  user process, or as a result of an implicit readlink during a name
  lookup by the process.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned. Suggested failure: EACCES for label mismatch or
  EPERM for lack of privilege.
*/
typedef int mpo_vnode_check_readlink_t(
  kauth_cred_t cred,
  struct vnode *vp,
  struct label *label
);
/**
  @brief Access control check for rename
  @param cred Subject credential
  @param dvp Directory vnode
  @param dlabel Policy label associated with dvp
  @param vp vnode to be renamed
  @param label Policy label associated with vp
  @param cnp Component name for vp
  @param tdvp Destination directory vnode
  @param tdlabel Policy label associated with tdvp
  @param tvp Overwritten vnode
  @param tlabel Policy label associated with tvp
  @param tcnp Destination component name

  Determine whether the subject identified by the credential should be allowed
  to rename the vnode vp to something else.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_vnode_check_rename_t(
  kauth_cred_t cred,
  struct vnode *dvp,
  struct label *dlabel,
  struct vnode *vp,
  struct label *label,
  struct componentname *cnp,
  struct vnode *tdvp,
  struct label *tdlabel,
  struct vnode *tvp,
  struct label *tlabel,
  struct componentname *tcnp
);
/**
  @brief Access control check for rename from
  @param cred Subject credential
  @param dvp Directory vnode
  @param dlabel Policy label associated with dvp
  @param vp vnode to be renamed
  @param label Policy label associated with vp
  @param cnp Component name for vp
  @see mpo_vnode_check_rename_t
  @see mpo_vnode_check_rename_to_t

  Determine whether the subject identified by the credential should be
  allowed to rename the vnode vp to something else.

  Due to VFS locking constraints (to make sure proper vnode locks are
  held during this entry point), the vnode relabel checks had to be
  split into two parts: relabel_from and relabel to.

  This hook is deprecated, mpo_vnode_check_rename_t should be used instead.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_vnode_check_rename_from_t(
  kauth_cred_t cred,
  struct vnode *dvp,
  struct label *dlabel,
  struct vnode *vp,
  struct label *label,
  struct componentname *cnp
);
/**
  @brief Access control check for rename to
  @param cred Subject credential
  @param dvp Directory vnode
  @param dlabel Policy label associated with dvp
  @param vp Overwritten vnode
  @param label Policy label associated with vp
  @param samedir Boolean; 1 if the source and destination directories are the same
  @param cnp Destination component name
  @see mpo_vnode_check_rename_t
  @see mpo_vnode_check_rename_from_t

  Determine whether the subject identified by the credential should be
  allowed to rename to the vnode vp, into the directory dvp, or to the
  name represented by cnp. If there is no existing file to overwrite,
  vp and label will be NULL.

  Due to VFS locking constraints (to make sure proper vnode locks are
  held during this entry point), the vnode relabel checks had to be
  split into two parts: relabel_from and relabel to.

  This hook is deprecated, mpo_vnode_check_rename_t should be used instead.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_vnode_check_rename_to_t(
  kauth_cred_t cred,
  struct vnode *dvp,
  struct label *dlabel,
  struct vnode *vp,     /* NULLOK */
  struct label *label,      /* NULLOK */
  int samedir,
  struct componentname *cnp
);
/**
  @brief Access control check for revoke
  @param cred Subject credential
  @param vp Object vnode
  @param label Policy label for vp

  Determine whether the subject identified by the credential can revoke
  access to the passed vnode.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned. Suggested failure: EACCES for label mismatch or
  EPERM for lack of privilege.
*/
typedef int mpo_vnode_check_revoke_t(
  kauth_cred_t cred,
  struct vnode *vp,
  struct label *label
);
/**
  @brief Access control check for searchfs
  @param cred Subject credential
  @param vp Object vnode
  @param vlabel Policy label for vp
  @param alist List of attributes used as search criteria

  Determine whether the subject identified by the credential can search the
  vnode using the searchfs system call.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.
*/
typedef int mpo_vnode_check_searchfs_t(
  kauth_cred_t cred,
  struct vnode *vp,
  struct label *vlabel,
  struct attrlist *alist
);
/**
  @brief Access control check for setting file attributes
  @param cred Subject credential
  @param vp Object vnode
  @param vlabel Policy label for vp
  @param alist List of attributes to set

  Determine whether the subject identified by the credential can set
  various attributes of the specified vnode, or the filesystem or volume on
  which that vnode resides. See <sys/attr.h> for definitions of the
  attributes.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned. Suggested failure: EACCES for label mismatch or
  EPERM for lack of privilege. Access control covers all attributes requested
  with this call.
*/
typedef int mpo_vnode_check_setattrlist_t(
  kauth_cred_t cred,
  struct vnode *vp,
  struct label *vlabel,
  struct attrlist *alist
);
/**
  @brief Access control check for setting extended attribute
  @param cred Subject credential
  @param vp Object vnode
  @param label Policy label for vp
  @param name Extended attribute name
  @param uio I/O structure pointer

  Determine whether the subject identified by the credential can set the
  extended attribute of passed name and passed namespace on the passed
  vnode. Policies implementing security labels backed into extended
  attributes may want to provide additional protections for those
  attributes. Additionally, policies should avoid making decisions based
  on the data referenced from uio, as there is a potential race condition
  between this check and the actual operation. The uio may also be NULL
  if a delete operation is being performed.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned. Suggested failure: EACCES for label mismatch or
  EPERM for lack of privilege.
*/
typedef int mpo_vnode_check_setextattr_t(
  kauth_cred_t cred,
  struct vnode *vp,
  struct label *label,
  const char *name,
  struct uio *uio
);
/**
  @brief Access control check for setting flags
  @param cred Subject credential
  @param vp Object vnode
  @param label Policy label for vp
  @param flags File flags; see chflags(2)

  Determine whether the subject identified by the credential can set
  the passed flags on the passed vnode.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned. Suggested failure: EACCES for label mismatch or
  EPERM for lack of privilege.
*/
typedef int mpo_vnode_check_setflags_t(
  kauth_cred_t cred,
  struct vnode *vp,
  struct label *label,
  u_long flags
);
/**
  @brief Access control check for setting mode
  @param cred Subject credential
  @param vp Object vnode
  @param label Policy label for vp
  @param mode File mode; see chmod(2)

  Determine whether the subject identified by the credential can set
  the passed mode on the passed vnode.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned. Suggested failure: EACCES for label mismatch or
  EPERM for lack of privilege.
*/
typedef int mpo_vnode_check_setmode_t(
  kauth_cred_t cred,
  struct vnode *vp,
  struct label *label,
  mode_t mode
);
/**
  @brief Access control check for setting uid and gid
  @param cred Subject credential
  @param vp Object vnode
  @param label Policy label for vp
  @param uid User ID
  @param gid Group ID

  Determine whether the subject identified by the credential can set
  the passed uid and passed gid as file uid and file gid on the passed
  vnode. The IDs may be set to (-1) to request no update.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned. Suggested failure: EACCES for label mismatch or
  EPERM for lack of privilege.
*/
typedef int mpo_vnode_check_setowner_t(
  kauth_cred_t cred,
  struct vnode *vp,
  struct label *label,
  uid_t uid,
  gid_t gid
);
/**
  @brief Access control check for setting timestamps
  @param cred Subject credential
  @param vp Object vnode
  @param label Policy label for vp
  @param atime Access time; see utimes(2)
  @param mtime Modification time; see utimes(2)

  Determine whether the subject identified by the credential can set
  the passed access timestamps on the passed vnode.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned. Suggested failure: EACCES for label mismatch or
  EPERM for lack of privilege.
*/
typedef int mpo_vnode_check_setutimes_t(
  kauth_cred_t cred,
  struct vnode *vp,
  struct label *label,
  struct timespec atime,
  struct timespec mtime
);
/**
  @brief Access control check for stat
  @param active_cred Subject credential
  @param file_cred Credential associated with the struct fileproc
  @param vp Object vnode
  @param label Policy label for vp

  Determine whether the subject identified by the credential can stat
  the passed vnode. See stat(2) for more information.  The active_cred
  hold the credentials of the subject performing the operation, and
  file_cred holds the credentials of the subject that originally
  opened the file.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned. Suggested failure: EACCES for label mismatch or
  EPERM for lack of privilege.
*/
typedef int mpo_vnode_check_stat_t(
  struct ucred *active_cred,
  struct ucred *file_cred,  /* NULLOK */
  struct vnode *vp,
  struct label *label
);
/**
  @brief Access control check for truncate/ftruncate
  @param active_cred Subject credential
  @param file_cred Credential associated with the struct fileproc
  @param vp Object vnode
  @param label Policy label for vp

  Determine whether the subject identified by the credential can
  perform a truncate operation on the passed vnode.  The active_cred hold
  the credentials of the subject performing the operation, and
  file_cred holds the credentials of the subject that originally
  opened the file.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned. Suggested failure: EACCES for label mismatch or
  EPERM for lack of privilege.
*/
typedef int mpo_vnode_check_truncate_t(
  kauth_cred_t active_cred,
  kauth_cred_t file_cred, /* NULLOK */
  struct vnode *vp,
  struct label *label
);
/**
  @brief Access control check for binding UNIX domain socket
  @param cred Subject credential
  @param dvp Directory vnode
  @param dlabel Policy label for dvp
  @param cnp Component name for dvp
  @param vap vnode attributes for vap

  Determine whether the subject identified by the credential can perform a
  bind operation on a UNIX domain socket with the passed parent directory,
  passed name information, and passed attribute information.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned. Suggested failure: EACCES for label mismatch or
  EPERM for lack of privilege.
*/
typedef int mpo_vnode_check_uipc_bind_t(
  kauth_cred_t cred,
  struct vnode *dvp,
  struct label *dlabel,
  struct componentname *cnp,
  struct vnode_attr *vap
);
/**
  @brief Access control check for connecting UNIX domain socket
  @param cred Subject credential
  @param vp Object vnode
  @param label Policy label associated with vp

  Determine whether the subject identified by the credential can perform a
  connect operation on the passed UNIX domain socket vnode.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned. Suggested failure: EACCES for label mismatch or
  EPERM for lack of privilege.
*/
typedef int mpo_vnode_check_uipc_connect_t(
  kauth_cred_t cred,
  struct vnode *vp,
  struct label *label
);
/**
  @brief Access control check for deleting vnode
  @param cred Subject credential
  @param dvp Parent directory vnode
  @param dlabel Policy label for dvp
  @param vp Object vnode to delete
  @param label Policy label for vp
  @param cnp Component name for vp
  @see mpo_check_rename_to_t

  Determine whether the subject identified by the credential can delete
  a vnode from the passed parent directory and passed name information.
  This call may be made in a number of situations, including as a
  results of calls to unlink(2) and rmdir(2). Policies implementing
  this entry point should also implement mpo_check_rename_to to
  authorize deletion of objects as a result of being the target of a rename.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned. Suggested failure: EACCES for label mismatch or
  EPERM for lack of privilege.
*/
typedef int mpo_vnode_check_unlink_t(
  kauth_cred_t cred,
  struct vnode *dvp,
  struct label *dlabel,
  struct vnode *vp,
  struct label *label,
  struct componentname *cnp
);
/**
  @brief Associate a pipe label with a vnode
  @param cred User credential for the process that opened the pipe
  @param cpipe Pipe structure
  @param pipelabel Label associated with pipe
  @param vp Vnode to label
  @param vlabel Label associated with vp

  Associate label information for the vnode, vp, with the label of
  the pipe described by the pipe structure cpipe.
  The label should be stored in the supplied vlabel parameter.
*/
typedef void mpo_vnode_label_associate_pipe_t(
  struct ucred *cred,
  struct pipe *cpipe,
  struct label *pipelabel,
  struct vnode *vp,
  struct label *vlabel
);
/**
  @brief Associate a POSIX semaphore label with a vnode
  @param cred User credential for the process that create psem
  @param psem POSIX semaphore structure
  @param psemlabel Label associated with psem
  @param vp Vnode to label
  @param vlabel Label associated with vp

  Associate label information for the vnode, vp, with the label of
  the POSIX semaphore described by psem.
  The label should be stored in the supplied vlabel parameter.
*/
typedef void mpo_vnode_label_associate_posixsem_t(
  struct ucred *cred,
  struct pseminfo *psem,
  struct label *psemlabel,
  struct vnode *vp,
  struct label *vlabel
);
/**
  @brief Associate a POSIX shared memory label with a vnode
  @param cred User credential for the process that created pshm
  @param pshm POSIX shared memory structure
  @param pshmlabel Label associated with pshm
  @param vp Vnode to label
  @param vlabel Label associated with vp

  Associate label information for the vnode, vp, with the label of
  the POSIX shared memory region described by pshm.
  The label should be stored in the supplied vlabel parameter.
*/
typedef void mpo_vnode_label_associate_posixshm_t(
  struct ucred *cred,
  struct pshminfo *pshm,
  struct label *pshmlabel,
  struct vnode *vp,
  struct label *vlabel
);
/**
  @brief Associate a socket label with a vnode
  @param cred User credential for the process that opened the socket
  @param so Socket structure
  @param solabel Label associated with so
  @param vp Vnode to label
  @param vlabel Label associated with vp

  Associate label information for the vnode, vp, with the label of
  the open socket described by the socket structure so.
  The label should be stored in the supplied vlabel parameter.
*/
typedef void mpo_vnode_label_associate_socket_t(
  kauth_cred_t cred,
  socket_t so,
  struct label *solabel,
  struct vnode *vp,
  struct label *vlabel
);
/**
  @brief Find deatched signatures for a shared library
  @param p file trying to find the signature
  @param vp The vnode to relabel
  @param offset offset in the macho that the signature is requested for (for fat binaries)
  @param label Existing vnode label

*/
typedef int mpo_vnode_find_sigs_t(
  struct proc *p,
  struct vnode *vp,
  off_t offset,
  struct label *label
);
/**
  @brief Create a new vnode, backed by extended attributes
  @param cred User credential for the creating process
  @param mp File system mount point
  @param mntlabel File system mount point label
  @param dvp Parent directory vnode
  @param dlabel Parent directory vnode label
  @param vp Newly created vnode
  @param vlabel Label to associate with the new vnode
  @param cnp Component name for vp

  Write out the label for the newly created vnode, most likely storing
  the results in a file system extended attribute.  Most policies will
  derive the new vnode label using information from a combination
  of the subject (user) credential, the file system label, the parent
  directory label, and potentially the path name component.

  @return If the operation succeeds, store the new label in vlabel and
  return 0.  Otherwise, return an appropriate errno value.
*/
typedef int mpo_vnode_notify_create_t(
  kauth_cred_t cred,
  struct mount *mp,
  struct label *mntlabel,
  struct vnode *dvp,
  struct label *dlabel,
  struct vnode *vp,
  struct label *vlabel,
  struct componentname *cnp
);

/**
  @brief Inform MAC policies that a vnode has been opened
  @param cred User credential for the creating process
  @param vp vnode opened
  @param label Policy label for the vp
  @param acc_mode open(2) access mode used

  Inform Mac policies that a vnode have been successfully opened
  (passing all MAC polices and DAC).
*/
typedef void mpo_vnode_notify_open_t(
  kauth_cred_t cred,
  struct vnode *vp,
  struct label *label,
  int acc_mode
);

/**
  @brief Inform MAC policies that a vnode has been renamed
  @param cred User credential for the renaming process
  @param vp Vnode that's being renamed
  @param label Policy label for vp
  @param dvp Parent directory for the destination
  @param dlabel Policy label for dvp
  @param cnp Component name for the destination

  Inform MAC policies that a vnode has been renamed.
 */
typedef void mpo_vnode_notify_rename_t(
  kauth_cred_t cred,
  struct vnode *vp,
  struct label *label,
  struct vnode *dvp,
  struct label *dlabel,
  struct componentname *cnp
);

/**
  @brief Inform MAC policies that a vnode has been linked
  @param cred User credential for the renaming process
  @param dvp Parent directory for the destination
  @param dlabel Policy label for dvp
  @param vp Vnode that's being linked
  @param vlabel Policy label for vp
  @param cnp Component name for the destination

  Inform MAC policies that a vnode has been linked.
 */
typedef void mpo_vnode_notify_link_t(
  kauth_cred_t cred,
  struct vnode *dvp,
  struct label *dlabel,
  struct vnode *vp,
  struct label *vlabel,
  struct componentname *cnp
);

/**
  @brief Inform MAC policies that a pty slave has been granted
  @param p Responsible process
  @param tp tty data structure
  @param dev Major and minor numbers of device
  @param label Policy label for tp
  
  Inform MAC policies that a pty slave has been granted.
*/
typedef void mpo_pty_notify_grant_t(
  proc_t p,
  struct tty *tp,
  dev_t dev,
  struct label *label
);

/**
  @brief Access control check for kext loading
  @param cred Subject credential
  @param identifier Kext identifier

  Determine whether the subject identified by the credential can load the
  specified kext.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned. Suggested failure: EPERM for lack of privilege.
*/
typedef int mpo_kext_check_load_t(
  kauth_cred_t cred,
  const char *identifier
);

/**
  @brief Access control check for kext unloading
  @param cred Subject credential
  @param identifier Kext identifier

  Determine whether the subject identified by the credential can unload the
  specified kext.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned. Suggested failure: EPERM for lack of privilege.
*/
typedef int mpo_kext_check_unload_t(
  kauth_cred_t cred,
  const char *identifier
);

/**
  @brief Access control check for querying information about loaded kexts
  @param cred Subject credential

  Determine whether the subject identified by the credential can query
  information about loaded kexts.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.  Suggested failure: EPERM for lack of privilege.
*/
typedef int mpo_kext_check_query_t(
  kauth_cred_t cred
);

/**
  @brief Access control check for getting NVRAM variables.
  @param cred Subject credential
  @param name NVRAM variable to get

  Determine whether the subject identifier by the credential can get the
  value of the named NVRAM variable.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.  Suggested failure: EPERM for lack of privilege.
*/
typedef int mpo_iokit_check_nvram_get_t(
  kauth_cred_t cred,
  const char *name
);

/**
  @brief Access control check for setting NVRAM variables.
  @param cred Subject credential
  @param name NVRAM variable to set
  @param value The new value for the NVRAM variable

  Determine whether the subject identifier by the credential can set the
  value of the named NVRAM variable.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.  Suggested failure: EPERM for lack of privilege.
*/
typedef int mpo_iokit_check_nvram_set_t(
  kauth_cred_t cred,
  const char *name,
  io_object_t value
);

/**
  @brief Access control check for deleting NVRAM variables.
  @param cred Subject credential
  @param name NVRAM variable to delete

  Determine whether the subject identifier by the credential can delete the
  named NVRAM variable.

  @return Return 0 if access is granted, otherwise an appropriate value for
  errno should be returned.  Suggested failure: EPERM for lack of privilege.
*/
typedef int mpo_iokit_check_nvram_delete_t(
  kauth_cred_t cred,
  const char *name
);

struct mac_policy_ops {
  // mpo_audit_check_postselect_t    *mpo_audit_check_postselect;
  // mpo_audit_check_preselect_t   *mpo_audit_check_preselect;

  // mpo_bpfdesc_label_associate_t   *mpo_bpfdesc_label_associate;
  // mpo_bpfdesc_label_destroy_t   *mpo_bpfdesc_label_destroy;
  // mpo_bpfdesc_label_init_t    *mpo_bpfdesc_label_init;
  mpo_bpfdesc_check_receive_t   *mpo_bpfdesc_check_receive;

  // mpo_cred_check_label_update_execve_t  *mpo_cred_check_label_update_execve;
  // mpo_cred_check_label_update_t   *mpo_cred_check_label_update;
  // mpo_cred_check_visible_t    *mpo_cred_check_visible;
  // mpo_cred_label_associate_fork_t   *mpo_cred_label_associate_fork;
  // mpo_cred_label_associate_kernel_t *mpo_cred_label_associate_kernel;
  mpo_cred_label_associate_t    *mpo_cred_label_associate;
  // mpo_cred_label_associate_user_t   *mpo_cred_label_associate_user;
  // mpo_cred_label_destroy_t    *mpo_cred_label_destroy;
  // mpo_cred_label_externalize_audit_t  *mpo_cred_label_externalize_audit;
  // mpo_cred_label_externalize_t    *mpo_cred_label_externalize;
  // mpo_cred_label_init_t     *mpo_cred_label_init;
  // mpo_cred_label_internalize_t    *mpo_cred_label_internalize;
  // mpo_cred_label_update_execve_t    *mpo_cred_label_update_execve;
  // mpo_cred_label_update_t     *mpo_cred_label_update;

  // mpo_devfs_label_associate_device_t  *mpo_devfs_label_associate_device;
  // mpo_devfs_label_associate_directory_t *mpo_devfs_label_associate_directory;
  // mpo_devfs_label_copy_t      *mpo_devfs_label_copy;
  // mpo_devfs_label_destroy_t   *mpo_devfs_label_destroy;
  // mpo_devfs_label_init_t      *mpo_devfs_label_init;
  // mpo_devfs_label_update_t    *mpo_devfs_label_update;

  mpo_file_check_change_offset_t    *mpo_file_check_change_offset;
  mpo_file_check_create_t     *mpo_file_check_create;
  mpo_file_check_dup_t      *mpo_file_check_dup;
  mpo_file_check_fcntl_t      *mpo_file_check_fcntl;
  mpo_file_check_get_offset_t   *mpo_file_check_get_offset;
  mpo_file_check_get_t      *mpo_file_check_get;
  mpo_file_check_inherit_t    *mpo_file_check_inherit;
  mpo_file_check_ioctl_t      *mpo_file_check_ioctl;
  mpo_file_check_lock_t     *mpo_file_check_lock;
  mpo_file_check_mmap_downgrade_t   *mpo_file_check_mmap_downgrade;
  mpo_file_check_mmap_t     *mpo_file_check_mmap;
  mpo_file_check_receive_t    *mpo_file_check_receive;
  mpo_file_check_set_t      *mpo_file_check_set;
  mpo_file_label_init_t     *mpo_file_label_init;
  mpo_file_label_destroy_t    *mpo_file_label_destroy;
  mpo_file_label_associate_t    *mpo_file_label_associate;

  mpo_ifnet_check_label_update_t    *mpo_ifnet_check_label_update;
  mpo_ifnet_check_transmit_t    *mpo_ifnet_check_transmit;
  mpo_ifnet_label_associate_t   *mpo_ifnet_label_associate;
  mpo_ifnet_label_copy_t      *mpo_ifnet_label_copy;
  mpo_ifnet_label_destroy_t   *mpo_ifnet_label_destroy;
  mpo_ifnet_label_externalize_t   *mpo_ifnet_label_externalize;
  mpo_ifnet_label_init_t      *mpo_ifnet_label_init;
  mpo_ifnet_label_internalize_t   *mpo_ifnet_label_internalize;
  mpo_ifnet_label_update_t    *mpo_ifnet_label_update;
  mpo_ifnet_label_recycle_t   *mpo_ifnet_label_recycle;

  mpo_inpcb_check_deliver_t   *mpo_inpcb_check_deliver;
  mpo_inpcb_label_associate_t   *mpo_inpcb_label_associate;
  mpo_inpcb_label_destroy_t   *mpo_inpcb_label_destroy;
  mpo_inpcb_label_init_t      *mpo_inpcb_label_init;
  mpo_inpcb_label_recycle_t   *mpo_inpcb_label_recycle;
  mpo_inpcb_label_update_t    *mpo_inpcb_label_update;

  mpo_iokit_check_device_t    *mpo_iokit_check_device;

  // mpo_ipq_label_associate_t   *mpo_ipq_label_associate;
  // mpo_ipq_label_compare_t     *mpo_ipq_label_compare;
  // mpo_ipq_label_destroy_t     *mpo_ipq_label_destroy;
  // mpo_ipq_label_init_t      *mpo_ipq_label_init;
  // mpo_ipq_label_update_t      *mpo_ipq_label_update;

  // mpo_reserved_hook_t                     *mpo_reserved1;
  // mpo_reserved_hook_t                     *mpo_reserved2;
  // mpo_reserved_hook_t                     *mpo_reserved3;
  // mpo_reserved_hook_t                     *mpo_reserved4;
  // mpo_reserved_hook_t                     *mpo_reserved5;
  // mpo_reserved_hook_t                     *mpo_reserved6;
  // mpo_reserved_hook_t                     *mpo_reserved7;
  // mpo_reserved_hook_t                     *mpo_reserved8;
  // mpo_reserved_hook_t                     *mpo_reserved9;

  // mpo_mbuf_label_associate_bpfdesc_t  *mpo_mbuf_label_associate_bpfdesc;
  // mpo_mbuf_label_associate_ifnet_t  *mpo_mbuf_label_associate_ifnet;
  // mpo_mbuf_label_associate_inpcb_t  *mpo_mbuf_label_associate_inpcb;
  // mpo_mbuf_label_associate_ipq_t    *mpo_mbuf_label_associate_ipq;
  // mpo_mbuf_label_associate_linklayer_t  *mpo_mbuf_label_associate_linklayer;
  // mpo_mbuf_label_associate_multicast_encap_t *mpo_mbuf_label_associate_multicast_encap;
  // mpo_mbuf_label_associate_netlayer_t *mpo_mbuf_label_associate_netlayer;
  // mpo_mbuf_label_associate_socket_t *mpo_mbuf_label_associate_socket;
  // mpo_mbuf_label_copy_t     *mpo_mbuf_label_copy;
  // mpo_mbuf_label_destroy_t    *mpo_mbuf_label_destroy;
  // mpo_mbuf_label_init_t     *mpo_mbuf_label_init;

  mpo_mount_check_fsctl_t     *mpo_mount_check_fsctl;
  mpo_mount_check_getattr_t   *mpo_mount_check_getattr;
  mpo_mount_check_label_update_t    *mpo_mount_check_label_update;
  mpo_mount_check_mount_t     *mpo_mount_check_mount;
  mpo_mount_check_remount_t   *mpo_mount_check_remount;
  mpo_mount_check_setattr_t   *mpo_mount_check_setattr;
  mpo_mount_check_stat_t      *mpo_mount_check_stat;
  mpo_mount_check_umount_t    *mpo_mount_check_umount;
  mpo_mount_label_associate_t   *mpo_mount_label_associate;
  mpo_mount_label_destroy_t   *mpo_mount_label_destroy;
  mpo_mount_label_externalize_t   *mpo_mount_label_externalize;
  mpo_mount_label_init_t      *mpo_mount_label_init;
  mpo_mount_label_internalize_t   *mpo_mount_label_internalize;

  mpo_netinet_fragment_t      *mpo_netinet_fragment;
  mpo_netinet_icmp_reply_t    *mpo_netinet_icmp_reply;
  mpo_netinet_tcp_reply_t     *mpo_netinet_tcp_reply;

  // mpo_pipe_check_ioctl_t      *mpo_pipe_check_ioctl;
  // mpo_pipe_check_kqfilter_t   *mpo_pipe_check_kqfilter;
  // mpo_pipe_check_label_update_t   *mpo_pipe_check_label_update;
  // mpo_pipe_check_read_t     *mpo_pipe_check_read;
  // mpo_pipe_check_select_t     *mpo_pipe_check_select;
  // mpo_pipe_check_stat_t     *mpo_pipe_check_stat;
  // mpo_pipe_check_write_t      *mpo_pipe_check_write;
  // mpo_pipe_label_associate_t    *mpo_pipe_label_associate;
  // mpo_pipe_label_copy_t     *mpo_pipe_label_copy;
  // mpo_pipe_label_destroy_t    *mpo_pipe_label_destroy;
  // mpo_pipe_label_externalize_t    *mpo_pipe_label_externalize;
  // mpo_pipe_label_init_t     *mpo_pipe_label_init;
  // mpo_pipe_label_internalize_t    *mpo_pipe_label_internalize;
  // mpo_pipe_label_update_t     *mpo_pipe_label_update;

  // mpo_policy_destroy_t      *mpo_policy_destroy;
  // mpo_policy_init_t     *mpo_policy_init;
  // mpo_policy_initbsd_t      *mpo_policy_initbsd;
  // mpo_policy_syscall_t      *mpo_policy_syscall;

  mpo_system_check_sysctlbyname_t   *mpo_system_check_sysctlbyname;
  mpo_proc_check_inherit_ipc_ports_t  *mpo_proc_check_inherit_ipc_ports;
  + mpo_vnode_check_rename_t    *mpo_vnode_check_rename;
  mpo_kext_check_query_t      *mpo_kext_check_query;
  mpo_iokit_check_nvram_get_t   *mpo_iokit_check_nvram_get;
  mpo_iokit_check_nvram_set_t   *mpo_iokit_check_nvram_set;
  mpo_iokit_check_nvram_delete_t    *mpo_iokit_check_nvram_delete;
  mpo_proc_check_expose_task_t    *mpo_proc_check_expose_task;
  mpo_proc_check_set_host_special_port_t  *mpo_proc_check_set_host_special_port;
  mpo_proc_check_set_host_exception_port_t *mpo_proc_check_set_host_exception_port;
  // mpo_reserved_hook_t     *mpo_reserved11;
  // mpo_reserved_hook_t     *mpo_reserved12;
  // mpo_reserved_hook_t     *mpo_reserved13;
  // mpo_reserved_hook_t     *mpo_reserved14;
  // mpo_reserved_hook_t     *mpo_reserved15;
  // mpo_reserved_hook_t     *mpo_reserved16;
  // mpo_reserved_hook_t     *mpo_reserved17;
  // mpo_reserved_hook_t     *mpo_reserved18;
  // mpo_reserved_hook_t     *mpo_reserved19;
  // mpo_reserved_hook_t     *mpo_reserved20;
  // mpo_reserved_hook_t     *mpo_reserved21;
  // mpo_reserved_hook_t     *mpo_reserved22;

  mpo_posixsem_check_create_t   *mpo_posixsem_check_create;
  mpo_posixsem_check_open_t   *mpo_posixsem_check_open;
  mpo_posixsem_check_post_t   *mpo_posixsem_check_post;
  mpo_posixsem_check_unlink_t   *mpo_posixsem_check_unlink;
  mpo_posixsem_check_wait_t   *mpo_posixsem_check_wait;
  mpo_posixsem_label_associate_t    *mpo_posixsem_label_associate;
  mpo_posixsem_label_destroy_t    *mpo_posixsem_label_destroy;
  mpo_posixsem_label_init_t   *mpo_posixsem_label_init;
  mpo_posixshm_check_create_t   *mpo_posixshm_check_create;
  mpo_posixshm_check_mmap_t   *mpo_posixshm_check_mmap;
  mpo_posixshm_check_open_t   *mpo_posixshm_check_open;
  mpo_posixshm_check_stat_t   *mpo_posixshm_check_stat;
  mpo_posixshm_check_truncate_t   *mpo_posixshm_check_truncate;
  mpo_posixshm_check_unlink_t   *mpo_posixshm_check_unlink;
  mpo_posixshm_label_associate_t    *mpo_posixshm_label_associate;
  mpo_posixshm_label_destroy_t    *mpo_posixshm_label_destroy;
  mpo_posixshm_label_init_t   *mpo_posixshm_label_init;

  // mpo_proc_check_debug_t      *mpo_proc_check_debug;
  // mpo_proc_check_fork_t     *mpo_proc_check_fork;
  mpo_proc_check_get_task_name_t    *mpo_proc_check_get_task_name;
  mpo_proc_check_get_task_t   *mpo_proc_check_get_task;
  mpo_proc_check_getaudit_t   *mpo_proc_check_getaudit;
  mpo_proc_check_getauid_t    *mpo_proc_check_getauid;
  mpo_proc_check_getlcid_t    *mpo_proc_check_getlcid;
  // mpo_proc_check_mprotect_t   *mpo_proc_check_mprotect;
  mpo_proc_check_sched_t      *mpo_proc_check_sched;
  mpo_proc_check_setaudit_t   *mpo_proc_check_setaudit;
  mpo_proc_check_setauid_t    *mpo_proc_check_setauid;
  mpo_proc_check_setlcid_t    *mpo_proc_check_setlcid;
  mpo_proc_check_signal_t     *mpo_proc_check_signal;
  // mpo_proc_check_wait_t     *mpo_proc_check_wait;
  + mpo_proc_label_destroy_t    *mpo_proc_label_destroy;
  // mpo_proc_label_init_t     *mpo_proc_label_init;

  mpo_socket_check_accept_t   *mpo_socket_check_accept;
  mpo_socket_check_accepted_t   *mpo_socket_check_accepted;
  mpo_socket_check_bind_t     *mpo_socket_check_bind;
  mpo_socket_check_connect_t    *mpo_socket_check_connect;
  mpo_socket_check_create_t   *mpo_socket_check_create;
  mpo_socket_check_deliver_t    *mpo_socket_check_deliver;
  mpo_socket_check_kqfilter_t   *mpo_socket_check_kqfilter;
  mpo_socket_check_label_update_t   *mpo_socket_check_label_update;
  mpo_socket_check_listen_t   *mpo_socket_check_listen;
  mpo_socket_check_receive_t    *mpo_socket_check_receive;
  mpo_socket_check_received_t   *mpo_socket_check_received;
  mpo_socket_check_select_t   *mpo_socket_check_select;
  mpo_socket_check_send_t     *mpo_socket_check_send;
  mpo_socket_check_stat_t     *mpo_socket_check_stat;
  mpo_socket_check_setsockopt_t   *mpo_socket_check_setsockopt;
  mpo_socket_check_getsockopt_t   *mpo_socket_check_getsockopt;
  mpo_socket_label_associate_accept_t *mpo_socket_label_associate_accept;
  mpo_socket_label_associate_t    *mpo_socket_label_associate;
  mpo_socket_label_copy_t     *mpo_socket_label_copy;
  mpo_socket_label_destroy_t    *mpo_socket_label_destroy;
  mpo_socket_label_externalize_t    *mpo_socket_label_externalize;
  mpo_socket_label_init_t     *mpo_socket_label_init;
  mpo_socket_label_internalize_t    *mpo_socket_label_internalize;
  mpo_socket_label_update_t   *mpo_socket_label_update;

  mpo_socketpeer_label_associate_mbuf_t *mpo_socketpeer_label_associate_mbuf;
  mpo_socketpeer_label_associate_socket_t *mpo_socketpeer_label_associate_socket;
  mpo_socketpeer_label_destroy_t    *mpo_socketpeer_label_destroy;
  mpo_socketpeer_label_externalize_t  *mpo_socketpeer_label_externalize;
  mpo_socketpeer_label_init_t   *mpo_socketpeer_label_init;

  mpo_system_check_acct_t     *mpo_system_check_acct;
  mpo_system_check_audit_t    *mpo_system_check_audit;
  mpo_system_check_auditctl_t   *mpo_system_check_auditctl;
  mpo_system_check_auditon_t    *mpo_system_check_auditon;
  mpo_system_check_host_priv_t    *mpo_system_check_host_priv;
  mpo_system_check_nfsd_t     *mpo_system_check_nfsd;
  mpo_system_check_reboot_t   *mpo_system_check_reboot;
  mpo_system_check_settime_t    *mpo_system_check_settime;
  mpo_system_check_swapoff_t    *mpo_system_check_swapoff;
  mpo_system_check_swapon_t   *mpo_system_check_swapon;
  mpo_reserved_hook_t     *mpo_reserved31;

  mpo_sysvmsg_label_associate_t   *mpo_sysvmsg_label_associate;
  mpo_sysvmsg_label_destroy_t   *mpo_sysvmsg_label_destroy;
  mpo_sysvmsg_label_init_t    *mpo_sysvmsg_label_init;
  mpo_sysvmsg_label_recycle_t   *mpo_sysvmsg_label_recycle;
  mpo_sysvmsq_check_enqueue_t   *mpo_sysvmsq_check_enqueue;
  mpo_sysvmsq_check_msgrcv_t    *mpo_sysvmsq_check_msgrcv;
  mpo_sysvmsq_check_msgrmid_t   *mpo_sysvmsq_check_msgrmid;
  mpo_sysvmsq_check_msqctl_t    *mpo_sysvmsq_check_msqctl;
  mpo_sysvmsq_check_msqget_t    *mpo_sysvmsq_check_msqget;
  mpo_sysvmsq_check_msqrcv_t    *mpo_sysvmsq_check_msqrcv;
  mpo_sysvmsq_check_msqsnd_t    *mpo_sysvmsq_check_msqsnd;
  mpo_sysvmsq_label_associate_t   *mpo_sysvmsq_label_associate;
  mpo_sysvmsq_label_destroy_t   *mpo_sysvmsq_label_destroy;
  mpo_sysvmsq_label_init_t    *mpo_sysvmsq_label_init;
  mpo_sysvmsq_label_recycle_t   *mpo_sysvmsq_label_recycle;
  mpo_sysvsem_check_semctl_t    *mpo_sysvsem_check_semctl;
  mpo_sysvsem_check_semget_t    *mpo_sysvsem_check_semget;
  mpo_sysvsem_check_semop_t   *mpo_sysvsem_check_semop;
  mpo_sysvsem_label_associate_t   *mpo_sysvsem_label_associate;
  mpo_sysvsem_label_destroy_t   *mpo_sysvsem_label_destroy;
  mpo_sysvsem_label_init_t    *mpo_sysvsem_label_init;
  mpo_sysvsem_label_recycle_t   *mpo_sysvsem_label_recycle;
  mpo_sysvshm_check_shmat_t   *mpo_sysvshm_check_shmat;
  mpo_sysvshm_check_shmctl_t    *mpo_sysvshm_check_shmctl;
  mpo_sysvshm_check_shmdt_t   *mpo_sysvshm_check_shmdt;
  mpo_sysvshm_check_shmget_t    *mpo_sysvshm_check_shmget;
  mpo_sysvshm_label_associate_t   *mpo_sysvshm_label_associate;
  mpo_sysvshm_label_destroy_t   *mpo_sysvshm_label_destroy;
  mpo_sysvshm_label_init_t    *mpo_sysvshm_label_init;
  mpo_sysvshm_label_recycle_t   *mpo_sysvshm_label_recycle;

  // mpo_reserved_hook_t     *mpo_reserved23;
  // mpo_reserved_hook_t     *mpo_reserved24;
  // mpo_reserved_hook_t     *mpo_reserved25;
  // mpo_reserved_hook_t     *mpo_reserved26;
  // mpo_reserved_hook_t     *mpo_reserved27;
  // mpo_reserved_hook_t     *mpo_reserved28;
  // mpo_reserved_hook_t     *mpo_reserved29;
  // mpo_reserved_hook_t     *mpo_reserved30;

  mpo_iokit_check_hid_control_t   *mpo_iokit_check_hid_control;

  + mpo_vnode_check_access_t    *mpo_vnode_check_access;
  // mpo_vnode_check_chdir_t     *mpo_vnode_check_chdir;
  // mpo_vnode_check_chroot_t    *mpo_vnode_check_chroot;
  ??? mpo_vnode_check_create_t    *mpo_vnode_check_create;
  + mpo_vnode_check_deleteextattr_t   *mpo_vnode_check_deleteextattr;
  + mpo_vnode_check_exchangedata_t    *mpo_vnode_check_exchangedata;
  + mpo_vnode_check_exec_t      *mpo_vnode_check_exec;
  + mpo_vnode_check_getattrlist_t   *mpo_vnode_check_getattrlist;
  + mpo_vnode_check_getextattr_t    *mpo_vnode_check_getextattr;
  + mpo_vnode_check_ioctl_t     *mpo_vnode_check_ioctl;
  ??? mpo_vnode_check_kqfilter_t    *mpo_vnode_check_kqfilter;
  // mpo_vnode_check_label_update_t    *mpo_vnode_check_label_update;
  + mpo_vnode_check_link_t      *mpo_vnode_check_link;
  + mpo_vnode_check_listextattr_t   *mpo_vnode_check_listextattr;
  ??? mpo_vnode_check_lookup_t    *mpo_vnode_check_lookup;
  + mpo_vnode_check_open_t      *mpo_vnode_check_open;
  // mpo_vnode_check_read_t      *mpo_vnode_check_read;
  + mpo_vnode_check_readdir_t   *mpo_vnode_check_readdir;
  + mpo_vnode_check_readlink_t    *mpo_vnode_check_readlink;
  // mpo_vnode_check_rename_from_t   *mpo_vnode_check_rename_from;  deprecated
  // mpo_vnode_check_rename_to_t   *mpo_vnode_check_rename_to;  deprecated
  + mpo_vnode_check_revoke_t    *mpo_vnode_check_revoke;
  // mpo_vnode_check_select_t    *mpo_vnode_check_select;
  + mpo_vnode_check_setattrlist_t   *mpo_vnode_check_setattrlist;
  + mpo_vnode_check_setextattr_t    *mpo_vnode_check_setextattr;
  + mpo_vnode_check_setflags_t    *mpo_vnode_check_setflags;
  + mpo_vnode_check_setmode_t   *mpo_vnode_check_setmode;
  + mpo_vnode_check_setowner_t    *mpo_vnode_check_setowner;
  + mpo_vnode_check_setutimes_t   *mpo_vnode_check_setutimes;
  + mpo_vnode_check_stat_t      *mpo_vnode_check_stat;
  + mpo_vnode_check_truncate_t    *mpo_vnode_check_truncate;
  + mpo_vnode_check_unlink_t    *mpo_vnode_check_unlink;
  + mpo_vnode_check_write_t     *mpo_vnode_check_write;
  // mpo_vnode_label_associate_devfs_t *mpo_vnode_label_associate_devfs;
  // mpo_vnode_label_associate_extattr_t *mpo_vnode_label_associate_extattr;
  // mpo_vnode_label_associate_file_t  *mpo_vnode_label_associate_file;
  // mpo_vnode_label_associate_pipe_t  *mpo_vnode_label_associate_pipe;
  // mpo_vnode_label_associate_posixsem_t  *mpo_vnode_label_associate_posixsem;
  // mpo_vnode_label_associate_posixshm_t  *mpo_vnode_label_associate_posixshm;
  // mpo_vnode_label_associate_singlelabel_t *mpo_vnode_label_associate_singlelabel;
  // mpo_vnode_label_associate_socket_t  *mpo_vnode_label_associate_socket;
  // mpo_vnode_label_copy_t      *mpo_vnode_label_copy;
  // mpo_vnode_label_destroy_t   *mpo_vnode_label_destroy;
  // mpo_vnode_label_externalize_audit_t *mpo_vnode_label_externalize_audit;
  // mpo_vnode_label_externalize_t   *mpo_vnode_label_externalize;
  // mpo_vnode_label_init_t      *mpo_vnode_label_init;
  // mpo_vnode_label_internalize_t   *mpo_vnode_label_internalize;
  // mpo_vnode_label_recycle_t   *mpo_vnode_label_recycle;
  // mpo_vnode_label_store_t     *mpo_vnode_label_store;
  // mpo_vnode_label_update_extattr_t  *mpo_vnode_label_update_extattr;
  // mpo_vnode_label_update_t    *mpo_vnode_label_update;
  ??? mpo_vnode_notify_create_t   *mpo_vnode_notify_create;
  ??? mpo_vnode_check_signature_t   *mpo_vnode_check_signature;
  // mpo_vnode_check_uipc_bind_t   *mpo_vnode_check_uipc_bind;
  // mpo_vnode_check_uipc_connect_t    *mpo_vnode_check_uipc_connect;

  mpo_proc_check_run_cs_invalid_t   *mpo_proc_check_run_cs_invalid;
  mpo_proc_check_suspend_resume_t   *mpo_proc_check_suspend_resume;

  mpo_thread_userret_t      *mpo_thread_userret;

  mpo_iokit_check_set_properties_t  *mpo_iokit_check_set_properties;

  mpo_system_check_chud_t     *mpo_system_check_chud;

  mpo_vnode_check_searchfs_t    *mpo_vnode_check_searchfs;

  + mpo_priv_check_t      *mpo_priv_check;
  // mpo_priv_grant_t      *mpo_priv_grant;

  mpo_proc_check_map_anon_t   *mpo_proc_check_map_anon;

  mpo_vnode_check_fsgetpath_t   *mpo_vnode_check_fsgetpath;

  mpo_iokit_check_open_t      *mpo_iokit_check_open;

  mpo_proc_check_ledger_t     *mpo_proc_check_ledger;

  // mpo_vnode_notify_rename_t   *mpo_vnode_notify_rename;

  // mpo_reserved_hook_t     *mpo_reserved32;
  // mpo_reserved_hook_t     *mpo_reserved33;

  mpo_system_check_kas_info_t   *mpo_system_check_kas_info;

  mpo_proc_check_cpumon_t     *mpo_proc_check_cpumon;

  // mpo_vnode_notify_open_t     *mpo_vnode_notify_open;  // we use check_open

  mpo_system_check_info_t     *mpo_system_check_info;

  mpo_pty_notify_grant_t      *mpo_pty_notify_grant;
  mpo_pty_notify_close_t      *mpo_pty_notify_close;

  mpo_vnode_find_sigs_t     *mpo_vnode_find_sigs;

  // mpo_kext_check_load_t     *mpo_kext_check_load;  // should be covered by priv_check
  // mpo_kext_check_unload_t     *mpo_kext_check_unload;  // should be covered by priv_check

  mpo_proc_check_proc_info_t    *mpo_proc_check_proc_info;
  // mpo_vnode_notify_link_t     *mpo_vnode_notify_link;  // we use check_link
  mpo_iokit_check_filter_properties_t *mpo_iokit_check_filter_properties;
  mpo_iokit_check_get_property_t    *mpo_iokit_check_get_property;
};

/**
   @brief MAC policy handle type

   The MAC handle is used to uniquely identify a loaded policy within
   the MAC Framework.

   A variable of this type is set by mac_policy_register().
 */
typedef unsigned int mac_policy_handle_t;

#define mpc_t struct mac_policy_conf *

/**
  @brief Mac policy configuration

  This structure specifies the configuration information for a
  MAC policy module.  A policy module developer must supply
  a short unique policy name, a more descriptive full name, a list of label
  namespaces and count, a pointer to the registered enty point operations,
  any load time flags, and optionally, a pointer to a label slot identifier.

  The Framework will update the runtime flags (mpc_runtime_flags) to
  indicate that the module has been registered.

  If the label slot identifier (mpc_field_off) is NULL, the Framework
  will not provide label storage for the policy.  Otherwise, the
  Framework will store the label location (slot) in this field.

  The mpc_list field is used by the Framework and should not be
  modified by policies.
*/
/* XXX - reorder these for better aligment on 64bit platforms */
struct mac_policy_conf {
  const char    *mpc_name;    /** policy name */
  const char    *mpc_fullname;    /** full name */
  const char    **mpc_labelnames; /** managed label namespaces */
  unsigned int     mpc_labelname_count; /** number of managed label namespaces */
  struct mac_policy_ops *mpc_ops;   /** operation vector */
  int      mpc_loadtime_flags;  /** load time flags */
  int     *mpc_field_off;   /** label slot */
  int      mpc_runtime_flags; /** run time flags */
  mpc_t      mpc_list;    /** List reference */
  void      *mpc_data;    /** module data */
};

/**
   @brief MAC policy module registration routine

   This function is called to register a policy with the
   MAC framework.  A policy module will typically call this from the
   Darwin KEXT registration routine.
 */
int mac_policy_register(struct mac_policy_conf *mpc,
    mac_policy_handle_t *handlep, void *xd);

/**
   @brief MAC policy module de-registration routine

   This function is called to de-register a policy with theD
   MAC framework.  A policy module will typically call this from the
   Darwin KEXT de-registration routine.
 */
int mac_policy_unregister(mac_policy_handle_t handle);

/*
 * Framework entry points for the policies to add audit data.
 */
int mac_audit_text(char *text, mac_policy_handle_t handle);

/*
 * Calls to assist with use of Apple XATTRs within policy modules.
 */
int mac_vnop_setxattr(struct vnode *, const char *, char *, size_t);
int mac_vnop_getxattr(struct vnode *, const char *, char *, size_t,
        size_t *);
int mac_vnop_removexattr(struct vnode *, const char *);

/*
 * Arbitrary limit on how much data will be logged by the audit
 * entry points above.
 */
#define MAC_AUDIT_DATA_LIMIT  1024

/*
 * Values returned by mac_audit_{pre,post}select. To combine the responses
 * of the security policies into a single decision,
 * mac_audit_{pre,post}select() choose the greatest value returned.
 */
#define MAC_AUDIT_DEFAULT 0 /* use system behavior */
#define MAC_AUDIT_NO    1 /* force not auditing this event */
#define MAC_AUDIT_YES   2 /* force auditing this event */

//  \defgroup mpc_loadtime_flags Flags for the mpc_loadtime_flags field

/**
  @name Flags for the mpc_loadtime_flags field
  @see mac_policy_conf

  This is the complete list of flags that are supported by the
  mpc_loadtime_flags field of the mac_policy_conf structure.  These
  flags specify the load time behavior of MAC Framework policy
  modules.
*/

/*@{*/

/**
  @brief Flag to indicate registration preference

  This flag indicates that the policy module must be loaded and
  initialized early in the boot process. If the flag is specified,
  attempts to register the module following boot will be rejected. The
  flag may be used by policies that require pervasive labeling of all
  system objects, and cannot handle objects that have not been
  properly initialized by the policy.
 */
#define MPC_LOADTIME_FLAG_NOTLATE 0x00000001

/**
  @brief Flag to indicate unload preference

  This flag indicates that the policy module may be unloaded. If this
  flag is not set, then the policy framework will reject requests to
  unload the module. This flag might be used by modules that allocate
  label state and are unable to free that state at runtime, or for
  modules that simply do not want to permit unload operations.
*/
#define MPC_LOADTIME_FLAG_UNLOADOK  0x00000002

/**
  @brief Unsupported

  XXX This flag is not yet supported.
*/
#define MPC_LOADTIME_FLAG_LABELMBUFS  0x00000004

/**
  @brief Flag to indicate a base policy

  This flag indicates that the policy module is a base policy. Only
  one module can declare itself as base, otherwise the boot process
  will be halted.
 */
#define MPC_LOADTIME_BASE_POLICY  0x00000008

/*@}*/

/**
  @brief Policy registration flag
  @see mac_policy_conf

  This flag indicates that the policy module has been successfully
  registered with the TrustedBSD MAC Framework.  The Framework will
  set this flag in the mpc_runtime_flags field of the policy's
  mac_policy_conf structure after registering the policy.
 */
#define MPC_RUNTIME_FLAG_REGISTERED 0x00000001

/*
 * Depends on POLICY_VER
 */

#ifndef POLICY_VER
#define POLICY_VER  1.0
#endif

#define MAC_POLICY_SET(handle, mpops, mpname, mpfullname, lnames, lcount, slot, lflags, rflags) \
  static struct mac_policy_conf mpname##_mac_policy_conf = {  \
    .mpc_name   = #mpname,      \
    .mpc_fullname   = mpfullname,     \
    .mpc_labelnames   = lnames,     \
    .mpc_labelname_count  = lcount,     \
    .mpc_ops    = mpops,      \
    .mpc_loadtime_flags = lflags,     \
    .mpc_field_off    = slot,       \
    .mpc_runtime_flags  = rflags      \
  };                \
                  \
  static kern_return_t            \
  kmod_start(kmod_info_t *ki, void *xd)       \
  {               \
    return mac_policy_register(&mpname##_mac_policy_conf, \
        &handle, xd);         \
  }               \
                  \
  static kern_return_t            \
  kmod_stop(kmod_info_t *ki, void *xd)        \
  {               \
    return mac_policy_unregister(handle);     \
  }               \
                  \
  extern kern_return_t _start(kmod_info_t *ki, void *data); \
  extern kern_return_t _stop(kmod_info_t *ki, void *data);  \
                  \
  KMOD_EXPLICIT_DECL(security.mpname, POLICY_VER, _start, _stop)  \
  kmod_start_func_t *_realmain = kmod_start;      \
  kmod_stop_func_t *_antimain = kmod_stop;      \
  int _kext_apple_cc = __APPLE_CC__


#define LABEL_TO_SLOT(l, s) (l)->l_perpolicy[s]

/*
 * Policy interface to map a struct label pointer to per-policy data.
 * Typically, policies wrap this in their own accessor macro that casts an
 * intptr_t to a policy-specific data type.
 */
intptr_t        mac_label_get(struct label *l, int slot);
void            mac_label_set(struct label *l, int slot, intptr_t v);

#define mac_get_mpc(h)    (mac_policy_list.entries[h].mpc)

/**
  @name Flags for MAC allocator interfaces

  These flags are passed to the Darwin kernel allocator routines to
  indicate whether the allocation is permitted to block or not.
  Caution should be taken; some operations are not permitted to sleep,
  and some types of locks cannot be held when sleeping.
 */

/*@{*/

/**
    @brief Allocation operations may block

    If memory is not immediately available, the allocation routine
    will block (typically sleeping) until memory is available.

    @warning Inappropriate use of this flag may cause kernel panics.
 */
#define MAC_WAITOK  0

/**
    @brief Allocation operations may not block

    Rather than blocking, the allocator may return an error if memory
    is not immediately available.  This type of allocation will not
    sleep, preserving locking semantics.
 */
#define MAC_NOWAIT  1

/*@}*/

#endif /* !_SECURITY_MAC_POLICY_H_ */
