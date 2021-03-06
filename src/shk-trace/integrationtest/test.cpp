// Copyright 2017 Per Grön. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <cerrno>
#include <copyfile.h>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <spawn.h>
#include <string>
#include <sys/types.h>  // has to be before acl.h is included, for gid_t
#include <sys/acl.h>
#include <sys/attr.h>
#include <sys/fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syslimits.h>
#include <sys/time.h>
#include <sys/xattr.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>

#include <util/file_descriptor.h>

using guardid_t = uint64_t;
static constexpr int PROTECTION_CLASS_DEFAULT = -1;
static constexpr int GUARD_DUP = 2;

extern "C" char **environ;
extern "C" int __chmod_extended(
    const char *path,
    uid_t uid,
    gid_t gid,
    int mode,
    struct kauth_filesec *sec);
extern "C" int __close_nocancel(int fd);
extern "C" int __copyfile(
    const char *from, const char *to, int mode, int flags);
extern "C" int __delete(const char *path);
extern "C" int __fchmod_extended(
    int fd,
    uid_t uid,
    gid_t gid,
    int mode,
    struct kauth_filesec *sec);
extern "C" int __fcntl_nocancel(int fildes, int cmd, ...);
extern "C" int __fstat_extended(
    int fd,
    struct stat *s,
    struct kauth_filesec *sec,
    size_t *sec_size);
extern "C" int __fstat64_extended(
    int fd,
    struct stat64 *s,
    struct kauth_filesec *sec,
    size_t *sec_size);
extern "C" int guarded_close_np(int fd, const guardid_t *guard);
extern "C" ssize_t __getdirentries64(
    int fd, void *buf, size_t bufsize, long *position);
extern "C" int __guarded_open_dprotected_np(
    const char *path,
    const guardid_t *guard,
    u_int guardflags,
    int flags,
    int dpclass,
    int dpflags,
    int mode);
extern "C" int __guarded_open_np(
    const char *path,
    const guardid_t *guard,
    u_int guardflags,
    int flags,
    int mode);
extern "C" int __lstat_extended(
    const char *path,
    struct stat *s,
    struct kauth_filesec *sec,
    size_t *sec_size);
extern "C" int __lstat64_extended(
    const char *path,
    struct stat64 *s,
    struct kauth_filesec *sec,
    size_t *sec_size);
extern "C" int __mkfifo_extended(
    const char *, uid_t, gid_t, int, struct kauth_filesec *);
extern "C" int __mkdir_extended(
    const char *, uid_t, gid_t, int, struct kauth_filesec *);
extern "C" int __open_extended(
    const char *path,
    int flags,
    uid_t uid,
    gid_t gid,
    int mode,
    struct kauth_filesec *sec);
extern "C" int __open_nocancel(const char *, int, ...);
extern "C" int __openat_nocancel(
    int fd, const char *fname, int oflag, mode_t mode);
extern "C" int __pthread_chdir(const char *path);
extern "C" int __pthread_fchdir(int fd);
extern "C" int rename_ext(
    const char *from,
    const char *to,
    int flags);
extern "C" int __stat_extended(
    const char *path,
    struct stat *s,
    struct kauth_filesec *sec,
    size_t *sec_size);
extern "C" int __stat64_extended(
    const char *path,
    struct stat64 *s,
    struct kauth_filesec *sec,
    size_t *sec_size);
extern "C" int openbyid_np(fsid_t* fsid, fsobj_id_t* objid, int flags);

namespace {

std::string self_executable_path;

void die(const std::string &reason) {
  fprintf(stderr, "Fatal error: %s (%s)\n", reason.c_str(), strerror(errno));
  exit(1);
}

std::string getFdPath(int fd) {
  char file_path[PATH_MAX];
  if (fcntl(fd, F_GETPATH, file_path) == -1) {
    die("Failed to get fd path");
  }
  return file_path;
}

shk::FileDescriptor openFileForReading(const std::string &path) {
  int fd = open(path.c_str(), O_RDONLY);
  if (fd == -1) {
    die("Failed to open file for reading " + path);
  }
  return shk::FileDescriptor(fd);
}

shk::FileDescriptor openFileForWriting(const std::string &path) {
  int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
  if (fd == -1) {
    die("Failed to open file writing " + path);
  }
  return shk::FileDescriptor(fd);
}

void testAccess() {
  if (access("input", 0) != 0) {
    die("access failed");
  }
}

void testAccessxNp() {
  const char *fn = "/usr";
  size_t fn_size = strlen(fn);

  size_t accessx_buffer_size = sizeof(accessx_descriptor) + fn_size;
  uint8_t *accessx_buffer = reinterpret_cast<uint8_t *>(
      alloca(accessx_buffer_size));
  memcpy(
    accessx_buffer + sizeof(accessx_descriptor),
    fn,
    fn_size + 1);

  accessx_descriptor *ad = reinterpret_cast<accessx_descriptor *>(
      accessx_buffer);
  bzero(ad, sizeof(accessx_descriptor));

  ad->ad_name_offset = sizeof(accessx_descriptor);

  int result;
  if (accessx_np(ad, accessx_buffer_size, &result, (uid_t) -1) == -1) {
    die("accessx_np failed");
  }
}

void testChdir() {
  if (chdir("/usr") != 0) {
    die("chdir failed");
  }
  access("nonexisting_path_just_for_testing", 0);
}

void testChdirOtherThread() {
  std::thread([] {
    if (chdir("/usr") != 0) {
      die("chdir failed");
    }
  }).join();
  access("nonexisting_path_just_for_testing", 0);
}

void testChdirFail() {
  if (chdir("/lalalala_nonexistent_just_for_testing") == 0) {
    die("chdir succeeded");
  }
  access("nonexisting_path_just_for_testing", 0);
}

void testChflags() {
  chflags("input", 0);
}

void testChmod() {
  chmod("input", 0555);
}

void testChmodExtended() {
  struct kauth_filesec filesec{ 0 };
  filesec.fsec_magic = KAUTH_FILESEC_MAGIC;
  __chmod_extended("input", getuid(), getgid(), 0555, &filesec);
}

void testChown() {
  chown("input", getuid(), getgid());
}

void testChroot() {
  // This syscall only works for root, but that's okay because it is a
  // restricted/illegal operation even if it fails.
  chroot("/");
}

void testClose() {
  auto usr_fd = openFileForReading("/usr");
  auto usr_fd_num = usr_fd.get();
  if (close(usr_fd.release()) != 0) {
    die("close failed");
  }

  // usr_fd_num is not a valid file descriptor anymore. This should fail.
  if (faccessat(usr_fd_num, "local", 0, 0) != -1 || errno != EBADF) {
    die("faccessat did not fail with EBADF error");
  }
}

void testCloseNocancel() {
  auto usr_fd = openFileForReading("/usr");
  auto usr_fd_num = usr_fd.get();
  if (__close_nocancel(usr_fd.release()) != 0) {
    die("close_nocancel failed");
  }

  // usr_fd_num is not a valid file descriptor anymore. This should fail.
  if (faccessat(usr_fd_num, "local", 0, 0) != -1 || errno != EBADF) {
    die("faccessat did not fail with EBADF error");
  }
}

void testCopyfile() {
  if (__copyfile("input", "output", 0555, 0) != -1) {
    // copyfile is not supported on HFS+, so it's expected to fail.
    die("copyfile succeeded");
  }
}

void testDelete() {
  // Carbon semantics delete. It is not supported by shk-trace
  __delete("input");
}

void testDup() {
  auto usr_fd = openFileForReading("/usr");
  auto duped_fd = shk::FileDescriptor(dup(usr_fd.get()));
  if (duped_fd.get() == -1) {
    die("dup failed");
  }

  if (openat(
      duped_fd.get(), "nonexisting_path_just_for_testing", O_RDONLY) != -1) {
    die("openat succeeded");
  }
}

void testDup2() {
  auto usr_fd = openFileForReading("/usr");

  int new_fd_num = 123;
  if (dup2(usr_fd.get(), new_fd_num) == -1) {
    die("dup2 failed");
  }
  auto duped_fd = shk::FileDescriptor(new_fd_num);

  if (openat(
      duped_fd.get(), "nonexisting_path_just_for_testing", O_RDONLY) != -1) {
    die("openat succeeded");
  }
}

void testExchangedata() {
  exchangedata("input", "output", 0);
}

void testExecve() {
  char *argv[] = { const_cast<char *>("true"), nullptr };
  char *environ[] = { nullptr };
  execve("/usr/bin/true", argv, environ);
  die("execve should not return");
}

void testFaccessat() {
  auto usr_fd = openFileForReading("/usr");

  faccessat(usr_fd.get(), "nonexisting_path_just_for_testing", 0, 0);
}

void testFchdir() {
  auto usr_fd = openFileForReading("/usr");
  if (fchdir(usr_fd.get()) != 0) {
    die("fchdir failed");
  }
  access("nonexisting_path_just_for_testing", 0);
}

void testFchflags() {
  auto input_fd = openFileForReading("input");
  fchflags(input_fd.get(), 0);
}

void testFchmod() {
  auto input_fd = openFileForReading("input");
  fchmod(input_fd.get(), 0555);
}

void testFchmodat() {
  auto dir_fd = openFileForReading("dir");
  fchmodat(dir_fd.get(), "input", 0555, 0);
}

void testFchmodExtended() {
  auto input_fd = openFileForReading("input");
  struct kauth_filesec filesec{ 0 };
  filesec.fsec_magic = KAUTH_FILESEC_MAGIC;
  __fchmod_extended(input_fd.get(), getuid(), getgid(), 0555, &filesec);
}

void testFchown() {
  auto input_fd = openFileForReading("input");
  fchown(input_fd.get(), getuid(), getgid());
}

void testFchownat() {
  auto dir_fd = openFileForReading("dir");
  fchownat(dir_fd.get(), "input", getuid(), getgid(), 0);
}

void testFcntlDisableCloexec() {
  auto dir_fd = shk::FileDescriptor(open("dir", O_RDONLY | O_CLOEXEC));
  if (dir_fd.get() == -1) {
    die("open of dir failed");
  }

  if (fcntl(dir_fd.get(), F_SETFD, 0) == -1) {
    die("fcntl failed");
  }

  const char *argv[] = {
      self_executable_path.c_str(),
      "open_cloexec_off:continuation",
      nullptr };
  auto fd_str = "fd=" + std::to_string(dir_fd.get());
  char *environ[] = { const_cast<char *>(fd_str.c_str()), nullptr };
  execve(self_executable_path.c_str(), const_cast<char **>(argv), environ);
  die("execve should not return");
}

void testFcntlDupfd() {
  auto original_fd = openFileForReading("dir");
  auto duped_fd = shk::FileDescriptor(fcntl(original_fd.get(), F_DUPFD));
  if (duped_fd.get() == -1) {
    die("dup failed");
  }

  const char *argv[] = {
      self_executable_path.c_str(),
      "open_cloexec_off:continuation",
      nullptr };
  auto fd_str = "fd=" + std::to_string(duped_fd.get());
  char *environ[] = { const_cast<char *>(fd_str.c_str()), nullptr };
  execve(self_executable_path.c_str(), const_cast<char **>(argv), environ);
  die("execve should not return");
}

void testFcntlDupfdCloexec() {
  auto original_fd = openFileForReading("/usr");
  auto duped_fd = shk::FileDescriptor(fcntl(original_fd.get(), F_DUPFD_CLOEXEC));
  if (duped_fd.get() == -1) {
    die("dup failed");
  }

  if (openat(
      duped_fd.get(), "nonexisting_path_just_for_testing", O_RDONLY) != -1) {
    die("openat succeeded");
  }
}

void testFcntlDupfdCloexecExec() {
  auto original_fd = openFileForReading("dir");
  auto duped_fd = shk::FileDescriptor(fcntl(original_fd.get(), F_DUPFD_CLOEXEC));
  if (duped_fd.get() == -1) {
    die("dup failed");
  }

  const char *argv[] = {
      self_executable_path.c_str(),
      "open_cloexec:continuation",
      nullptr };
  auto fd_str = "fd=" + std::to_string(duped_fd.get());
  char *environ[] = { const_cast<char *>(fd_str.c_str()), nullptr };
  execve(self_executable_path.c_str(), const_cast<char **>(argv), environ);
  die("execve should not return");
}

void testFcntlEnableCloexec() {
  auto dir_fd = shk::FileDescriptor(open("dir", O_RDONLY));
  if (dir_fd.get() == -1) {
    die("open of dir failed");
  }

  if (fcntl(dir_fd.get(), F_SETFD, FD_CLOEXEC) == -1) {
    die("fcntl failed");
  }

  const char *argv[] = {
      self_executable_path.c_str(),
      "open_cloexec:continuation",
      nullptr };
  auto fd_str = "fd=" + std::to_string(dir_fd.get());
  char *environ[] = { const_cast<char *>(fd_str.c_str()), nullptr };
  execve(self_executable_path.c_str(), const_cast<char **>(argv), environ);
  die("execve should not return");
}

void testFcntlNocancelDupfd() {
  auto original_fd = openFileForReading("dir");
  auto duped_fd = shk::FileDescriptor(
      __fcntl_nocancel(original_fd.get(), F_DUPFD));
  if (duped_fd.get() == -1) {
    die("dup failed");
  }

  const char *argv[] = {
      self_executable_path.c_str(),
      "open_cloexec_off:continuation",
      nullptr };
  auto fd_str = "fd=" + std::to_string(duped_fd.get());
  char *environ[] = { const_cast<char *>(fd_str.c_str()), nullptr };
  execve(self_executable_path.c_str(), const_cast<char **>(argv), environ);
  die("execve should not return");
}

void testFgetattrlist() {
  auto input_fd = openFileForReading("input");

  struct attrlist al;
  char buf[1024];
  fgetattrlist(input_fd.get(), &al, buf, sizeof(buf), 0);
}

void testFgetxattr() {
  auto input_fd = openFileForReading("input");

  char buf[1024];
  fgetxattr(input_fd.get(), "test", buf, sizeof(buf), 0, 0);
}

void testFhopen() {
  if (fhopen(nullptr, 0) != -1) {
    die("fhopen succeeded");
  }
}

void testFlistxattr() {
  auto input_fd = openFileForReading("input");

  char buf[1024];
  flistxattr(input_fd.get(), buf, sizeof(buf), 0);
}

void testFlock() {
  auto input_fd = openFileForReading("input");
  if (flock(input_fd.get(), LOCK_UN) != 0) {
    die("flock failed");
  }
}

void testForkOrVforkInheritFd(pid_t (*fork_fn)()) {
  // Verify that file descriptors are inherited

  auto usr_fd = openFileForReading("/usr");

  pid_t pid = fork_fn();
  if (pid == -1) {
    die("Failed to fork");
  } else if (pid == 0) {
    // In child
    if (openat(
        usr_fd.get(), "nonexisting_path_just_for_testing", O_RDONLY) != -1) {
      die("openat succeeded");
    }
  } else {
    // In parent
    int status;
    if (waitpid(pid, &status, 0) == -1) {
      die("Failed to wait for child");
    }
    if (status != 0) {
      die("Child failed");
    }
  }
}

void testForkInheritFd() {
  testForkOrVforkInheritFd(&fork);
}

void testFpathconf() {
  auto input_fd = openFileForReading("input");
  fpathconf(input_fd.get(), _PC_LINK_MAX);
}

void testFremovexattr() {
  auto input_fd = openFileForReading("input");
  fremovexattr(input_fd.get(), "test", 0);
}

void testFsetattrlist() {
  auto input_fd = openFileForReading("input");

  struct attrlist al{};
  al.bitmapcount = ATTR_BIT_MAP_COUNT;
  al.commonattr = ATTR_CMN_FNDRINFO;

  char buf[1024];
  fsetattrlist(input_fd.get(), &al, buf, sizeof(buf), 0);
}

void testFsetxattr() {
  auto input_fd = openFileForReading("input");

  fsetxattr(input_fd.get(), "test", "", 0, 0, 0);
}

void testFstat() {
  auto input_fd = openFileForReading("input");
  struct stat s;
  fstat(input_fd.get(), &s);
}

void testFstat64() {
  auto input_fd = openFileForReading("input");
  struct stat64 s;
  fstat64(input_fd.get(), &s);
}

void testFstat64Extended() {
  auto input_fd = openFileForReading("input");
  struct kauth_filesec filesec{ 0 };
  filesec.fsec_magic = KAUTH_FILESEC_MAGIC;
  size_t sec_size = sizeof(filesec);
  struct stat64 s;
  __fstat64_extended(input_fd.get(), &s, &filesec, &sec_size);
}

void testFstatat() {
  auto dir_fd = openFileForReading("dir");
  struct stat s;
  fstatat(dir_fd.get(), "input", &s, 0);
}

void testFstatExtended() {
  auto input_fd = openFileForReading("input");
  struct kauth_filesec filesec{ 0 };
  filesec.fsec_magic = KAUTH_FILESEC_MAGIC;
  size_t sec_size = sizeof(filesec);
  struct stat s;
  __fstat_extended(input_fd.get(), &s, &filesec, &sec_size);
}

void testFutimes() {
  auto input_fd = openFileForReading("input");
  struct timeval times[] = { { 0, 0 }, { 0, 0 } };
  futimes(input_fd.get(), times);
}

void testGetattrlist() {
  struct attrlist al;
  char buf[1024];
  getattrlist("input", &al, buf, sizeof(buf), 0);
}

void testGetattrlistat() {
  auto dir_fd = openFileForReading("dir");

  struct attrlist al;
  char buf[1024];
  getattrlistat(dir_fd.get(), "input", &al, buf, sizeof(buf), 0);
}

void testGetattrlistbulk() {
  auto dir_fd = openFileForReading("dir");

  struct attrlist al{};
  bzero(&al, sizeof(al));
  al.bitmapcount = ATTR_BIT_MAP_COUNT;
  al.commonattr =
      ATTR_CMN_RETURNED_ATTRS |
      ATTR_CMN_NAME |
      ATTR_CMN_ERROR |
      ATTR_CMN_OBJTYPE;

  char buf[1024];

  if (getattrlistbulk(dir_fd.get(), &al, buf, sizeof(buf), 0) == -1) {
    die("getattrlistbulk failed");
  }
}

void testGetdirentries() {
  auto dir_fd = openFileForReading("dir");

  char buf[1024];
  long offset = 0;
  if (__getdirentries64(dir_fd.get(), buf, sizeof(buf), &offset) == -1) {
    die("getdirentries failed");
  }
}

void testGetdirentriesattr() {
  auto dir_fd = openFileForReading("dir");

  struct attrlist al{};
  bzero(&al, sizeof(al));
  al.bitmapcount = ATTR_BIT_MAP_COUNT;
  al.commonattr =
      ATTR_CMN_NAME |
      ATTR_CMN_OBJTYPE |
      ATTR_CMN_MODTIME |
      ATTR_CMN_ACCESSMASK;
  al.fileattr = ATTR_FILE_DATALENGTH;

  char buf[1024];
  unsigned int count = 1;
  unsigned int basep = 0;
  unsigned int new_state = 0;

  if (getdirentriesattr(
          dir_fd.get(),
          &al,
          buf,
          sizeof(buf),
          &count,
          &basep,
          &new_state,
          0) == -1) {
    die("getdirentriesattr failed");
  }
}

void testGetxattr() {
  char buf[1024];
  getxattr("input", "test", buf, sizeof(buf), 0, 0);
}

void testGuardedCloseNp() {
  int flags = O_RDONLY | O_CLOEXEC;
  guardid_t guard = GUARD_DUP;
  auto usr_fd = shk::FileDescriptor(__guarded_open_dprotected_np(
      "/usr", &guard, GUARD_DUP, flags, PROTECTION_CLASS_DEFAULT, 0, 0666));
  if (usr_fd.get() == -1) {
    die("guarded_open_dprotected_np failed");
  }

  auto usr_fd_num = usr_fd.get();
  guardid_t close_guard = GUARD_DUP;
  if (guarded_close_np(usr_fd.release(), &close_guard) != 0) {
    die("close failed");
  }

  // usr_fd_num is not a valid file descriptor anymore. This should fail.
  if (faccessat(usr_fd_num, "local", 0, 0) != -1 || errno != EBADF) {
    die("faccessat did not fail with EBADF error");
  }
}

void testGuardedOpenDprotectedNp() {
  // Don't check for an error code; some tests trigger an error intentionally.
  int flags = O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC;
  guardid_t guard = GUARD_DUP;
  shk::FileDescriptor(__guarded_open_dprotected_np(
      "input", &guard, GUARD_DUP, flags, PROTECTION_CLASS_DEFAULT, 0, 0666));
}

void testGuardedOpenNp() {
  // Don't check for an error code; some tests trigger an error intentionally.
  int flags = O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC;
  guardid_t guard = GUARD_DUP;
  shk::FileDescriptor(__guarded_open_np(
      "input", &guard, GUARD_DUP, flags, 0666));
}

void testLchown() {
  lchown("input", getuid(), getgid());
}

void testLink() {
  // Don't check for an error code; some tests trigger an error intentionally.
  link("input", "output");
}

void testLinkat() {
  auto dir1_fd = openFileForReading("dir1");
  auto dir2_fd = openFileForReading("dir2");
  // Don't check for an error code; some tests trigger an error intentionally.
  linkat(dir1_fd.get(), "input", dir2_fd.get(), "output", AT_SYMLINK_FOLLOW);
}

void testListxattr() {
  char buf[1024];
  listxattr("input", buf, sizeof(buf), 0);
}

void testLstat() {
  struct stat s;
  lstat("input", &s);
}

void testLstat64() {
  struct stat64 s;
  lstat64("input", &s);
}

void testLstat64Extended() {
  struct kauth_filesec filesec{ 0 };
  filesec.fsec_magic = KAUTH_FILESEC_MAGIC;
  size_t sec_size = sizeof(filesec);
  struct stat64 s;
  __lstat64_extended("input", &s, &filesec, &sec_size);
}

void testLstatExtended() {
  struct kauth_filesec filesec{ 0 };
  size_t sec_size = sizeof(filesec);
  filesec.fsec_magic = KAUTH_FILESEC_MAGIC;
  struct stat s;
  __lstat_extended("input", &s, &filesec, &sec_size);
}

void testMkdir() {
  // Don't check for an error code; some tests trigger an error intentionally.
  mkdir("output", 0666);
}

void testMkdirat() {
  auto dir_fd = openFileForReading("dir");
  // Don't check for an error code; some tests trigger an error intentionally.
  mkdirat(dir_fd.get(), "output", 0666);
}

void testMkdirExtended() {
  struct kauth_filesec filesec{ 0 };
  filesec.fsec_magic = KAUTH_FILESEC_MAGIC;
  __mkdir_extended("output", getuid(), getgid(), 0666, &filesec);
}

void testMkfifo() {
  // Don't check for an error code; some tests trigger an error intentionally.
  mkfifo("output", 0666);
}

void testMkfifoExtended() {
  struct kauth_filesec filesec{ 0 };
  filesec.fsec_magic = KAUTH_FILESEC_MAGIC;
  __mkfifo_extended("output", getuid(), getgid(), 0666, &filesec);
}

void testMknod() {
  if (mknod("some_dir/blah", 0, 0) == 0) {
    die("mknod succeeded");
  }
}

void testOpenat() {
  auto dir_fd = openFileForReading("dir");

  // Don't check for an error code; some tests trigger an error intentionally.
  shk::FileDescriptor(openat(dir_fd.get(), "input", O_RDONLY));
}

void testOpenatNocancel() {
  auto dir_fd = openFileForReading("dir");

  // Don't check for an error code; some tests trigger an error intentionally.
  shk::FileDescriptor(__openat_nocancel(dir_fd.get(), "input", O_RDONLY, 0));
}

void testOpenatWithOpenatFd() {
  auto dir_fd = openFileForReading("/");

  auto usr_fd = shk::FileDescriptor(openat(dir_fd.get(), "usr", O_RDONLY));
  if (usr_fd.get() == -1) {
    die("openat of /usr failed");
  }

  auto local_fd = shk::FileDescriptor(openat(usr_fd.get(), "shk_for_testing_only", O_RDONLY));
  if (local_fd.get() != -1 || errno != ENOENT) {
    die("openat of /usr/shk_for_testing_only succeeded");
  }
}

void testOpenbyidNp() {
  if (openbyid_np(nullptr, nullptr, 0) != -1) {
    die("openbyid_np succeeded");
  }
}

int parseFdFromEnviron() {
  for (char **env = environ; *env; env++) {
    char *v = *env;
    if (strlen(v) > 3 && v[0] == 'f' && v[1] == 'd' && v[2] == '=') {
      return atoi(v + 3);
    }
  }
  die("could not extract fd from environ");
  return -1;
}

void testOpenCloexec() {
  auto dir_fd = shk::FileDescriptor(open("dir", O_RDONLY | O_CLOEXEC));
  if (dir_fd.get() == -1) {
    die("open of dir failed");
  }

  const char *argv[] = {
      self_executable_path.c_str(),
      "open_cloexec:continuation",
      nullptr };
  auto fd_str = "fd=" + std::to_string(dir_fd.get());
  char *environ[] = { const_cast<char *>(fd_str.c_str()), nullptr };
  execve(self_executable_path.c_str(), const_cast<char **>(argv), environ);
  die("execve should not return");
}

void testOpenCloexecContinuation() {
  auto dir_fd = parseFdFromEnviron();

  auto fd = shk::FileDescriptor(openat(dir_fd, "input", O_RDONLY));
  if (fd.get() != -1 || errno != EBADF) {
    die("the cloexec'd fd should be closed by now");
  }
}

void testOpenCloexecOff() {
  auto dir_fd = shk::FileDescriptor(open("dir", O_RDONLY));
  if (dir_fd.get() == -1) {
    die("open of dir failed");
  }

  const char *argv[] = {
      self_executable_path.c_str(),
      "open_cloexec_off:continuation",
      nullptr };
  auto fd_str = "fd=" + std::to_string(dir_fd.get());
  char *environ[] = { const_cast<char *>(fd_str.c_str()), nullptr };
  execve(self_executable_path.c_str(), const_cast<char **>(argv), environ);
  die("execve should not return");
}

void testOpenCloexecOffContinuation() {
  auto dir_fd = shk::FileDescriptor(parseFdFromEnviron());

  auto fd = shk::FileDescriptor(openat(dir_fd.get(), "input", O_RDONLY));
  if (fd.get() == -1) {
    die("open failed");
  }
}

void testOpenCreate() {
  // Don't check for an error code; some tests trigger an error intentionally.
  int flags = O_WRONLY | O_CREAT | O_TRUNC;
  auto fd = shk::FileDescriptor(open("input", flags, 0666));
  if (fd.get() == -1) {
    die("open failed");
  }

  if (write(fd.get(), "yo", 2) != 2) {
    die("write failed");
  }
}

void testOpenCreateAndRead() {
  // Don't check for an error code; some tests trigger an error intentionally.
  int flags = O_RDWR | O_CREAT | O_TRUNC;
  auto fd = shk::FileDescriptor(open("input", flags, 0666));
  if (fd.get() == -1) {
    die("open failed");
  }

  if (write(fd.get(), "HA", 2) != 2) {
    die("write failed");
  }

  if (lseek(fd.get(), 0, SEEK_SET) == -1) {
    die("lseek failed");
  }

  char buf[16];
  if (read(fd.get(), buf, 2) != 2) {
    die("read failed");
  }
  buf[2] = 0;
  if (std::string(buf) != "HA") {
    die("expected to read 'HA'");
  }
}

void testOpenCreateExcl() {
  // Don't check for an error code; some tests trigger an error intentionally.
  int flags = O_WRONLY | O_CREAT | O_EXCL | O_TRUNC;
  auto fd = shk::FileDescriptor(open("input", flags, 0666));
  if (fd.get() == -1) {
    die("open failed");
  }

  if (write(fd.get(), "ye", 2) != 2) {
    die("write failed");
  }
}

void testOpenCreateExclAppend() {
  // Don't check for an error code; some tests trigger an error intentionally.
  int flags = O_WRONLY | O_CREAT | O_EXCL;
  auto fd = shk::FileDescriptor(open("input", flags, 0666));
  if (fd.get() == -1) {
    die("open failed");
  }

  if (write(fd.get(), "ye", 2) != 2) {
    die("write failed");
  }
}

void testOpendir() {
  DIR *dir = opendir("dir");
  readdir(dir);
  closedir(dir);
}

void testOpenDprotectedNp() {
  // Don't check for an error code; some tests trigger an error intentionally.
  int flags = O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC;
  shk::FileDescriptor(open_dprotected_np(
      "input", flags, PROTECTION_CLASS_DEFAULT, 0, 0666));
}

void testOpenExtended() {
  // Don't check for an error code; some tests trigger an error intentionally.
  struct kauth_filesec filesec{ 0 };
  filesec.fsec_magic = KAUTH_FILESEC_MAGIC;
  shk::FileDescriptor(__open_extended(
      "input", O_RDONLY, getuid(), getgid(), 0, &filesec));
}

void testOpenImplicitRead() {
  auto fd = shk::FileDescriptor(open("input", 0, 0));
  if (fd.get() == -1) {
    die("open failed");
  }

  char buf[16];
  if (read(fd.get(), buf, 2) != 2) {
    die("read failed");
  }
  buf[2] = 0;
  if (std::string(buf) != "hi") {
    die("expected to read 'hi'");
  }
}

void testOpenNocancel() {
  // Don't check for an error code; some tests trigger an error intentionally.
  shk::FileDescriptor(__open_nocancel("input", O_RDONLY, 0));
}

void testOpenPartialOverwrite() {
  auto fd = shk::FileDescriptor(open("input", O_WRONLY, 0));
  if (fd.get() == -1) {
    die("open failed");
  }

  if (write(fd.get(), "hi", 2) != 2) {
    die("write failed");
  }
}

void testOpenRead() {
  // Don't check for an error code; some tests trigger an error intentionally.
  shk::FileDescriptor(open("input", O_RDONLY, 0));
}

void testPathconf() {
  pathconf("input", _PC_LINK_MAX);
}

void testPosixSpawn() {
  pid_t pid;
  char *argv[] = { const_cast<char *>("/usr/bin/true"), nullptr };
  char *environ[] = { nullptr };
  int err = posix_spawn(
      &pid,
      "/usr/bin/true",
      nullptr,
      nullptr,
      argv,
      environ);
  if (err) {
    die("posix_spawn failed");
  }

  int status;
  if (waitpid(pid, &status, 0) == -1) {
    die("failed waiting for child process");
  }
}

void testPthreadChdir() {
  if (__pthread_chdir("/usr") != 0) {
    die("chdir failed");
  }
  access("nonexisting_path_just_for_testing", 0);
}

void testPthreadChdirOtherThread() {
  std::thread([]{
    if (__pthread_chdir("/usr") != 0) {
      die("chdir failed");
    }
  }).join();
  access("nonexisting_path_just_for_testing", 0);
}

void testPthreadChdirFail() {
  if (__pthread_chdir("/lalalala_nonexistent_just_for_testing") == 0) {
    die("chdir succeeded");
  }
  access("nonexisting_path_just_for_testing", 0);
}

void testPthreadFchdir() {
  auto usr_fd = openFileForReading("/usr");
  if (__pthread_fchdir(usr_fd.get()) != 0) {
    die("fchdir failed");
  }
  access("nonexisting_path_just_for_testing", 0);
}

void testPthreadFchdirOtherThread() {
  std::thread([]{
    auto usr_fd = openFileForReading("/usr");
    if (__pthread_fchdir(usr_fd.get()) != 0) {
      die("fchdir failed");
    }
  }).join();
  access("nonexisting_path_just_for_testing", 0);
}

void testReadlink() {
  char buf[1024];
  // Don't check for an error code; some tests trigger an error intentionally.
  readlink("input", buf, sizeof(buf));
}

void testReadlinkat() {
  auto dir_fd = openFileForReading("dir");
  char buf[1024];
  // Don't check for an error code; some tests trigger an error intentionally.
  readlinkat(dir_fd.get(), "../input", buf, sizeof(buf));
}

void testRemovexattr() {
  removexattr("input", "test", 0);
}

void testRename() {
  // Don't check for an error code; some tests trigger an error intentionally.
  rename("input", "output");
}

void testRenameat() {
  auto dir1_fd = openFileForReading("dir1");
  auto dir2_fd = openFileForReading("dir2");
  // Don't check for an error code; some tests trigger an error intentionally.
  renameat(dir1_fd.get(), "input", dir2_fd.get(), "output");
}

void testRenameatxNp() {
  auto dir1_fd = openFileForReading("dir1");
  auto dir2_fd = openFileForReading("dir2");
  // Don't check for an error code; some tests trigger an error intentionally.
  renameatx_np(dir1_fd.get(), "input", dir2_fd.get(), "output", 0);
}

void testRenameExt() {
  // This actually ends up being the same syscall as renameatx_np, but it's
  // tested separately anyway just in case.
  rename_ext("input", "output", 0);
}

void testRenamexNp() {
  // Don't check for an error code; some tests trigger an error intentionally.
  renamex_np("input", "output", 0);
}

void testRmdir() {
  rmdir("dir");
}

void testSearchfs() {
  struct fssearchblock sb{ 0 };
  struct searchstate ss{ 0 };
  unsigned long num_matches = 0;
  static constexpr int kMagicConstantMandatedByManPage = 0x08000103;
  searchfs(".", &sb, &num_matches, kMagicConstantMandatedByManPage, SRCHFS_START, &ss);
}

void testSetattrlist() {
  struct attrlist al{};
  al.bitmapcount = ATTR_BIT_MAP_COUNT;
  al.commonattr = ATTR_CMN_FNDRINFO;

  char buf[1024];
  setattrlist("input", &al, buf, sizeof(buf), 0);
}

void testSetxattr() {
  setxattr("input", "test", "", 0, 0, 0);
}

void testStat() {
  struct stat s;
  stat("input", &s);
}

void testStatExtended() {
  struct kauth_filesec filesec{ 0 };
  filesec.fsec_magic = KAUTH_FILESEC_MAGIC;
  size_t sec_size = sizeof(filesec);
  struct stat s;
  __stat_extended("input", &s, &filesec, &sec_size);
}

void testStat64() {
  struct stat64 s;
  stat64("input", &s);
}

void testStat64Extended() {
  struct kauth_filesec filesec{ 0 };
  filesec.fsec_magic = KAUTH_FILESEC_MAGIC;
  size_t sec_size = sizeof(filesec);
  struct stat64 s;
  __stat64_extended("input", &s, &filesec, &sec_size);
}

void testSymlink() {
  // Don't check for an error code; some tests trigger an error intentionally.
  symlink("input", "output");
}

void testSymlinkat() {
  auto dir_fd = openFileForReading("dir");
  // Don't check for an error code; some tests trigger an error intentionally.
  symlinkat("input", dir_fd.get(), "output");
}

void testTruncate() {
  // Don't check for an error code; some tests trigger an error intentionally.
  truncate("input", 123);
}

void testUndelete() {
  if (undelete("undelete_test") == 0) {
    die("undelete succeeded");
  }
}

void testUnlink() {
  if (unlink("input") != 0) {
    die("unlink failed");
  }
}

void testUnlinkat() {
  auto dir_fd = openFileForReading("dir");
  if (unlinkat(dir_fd.get(), "../input", 0) != 0) {
    die("unlinkat failed");
  }
}

void testUnlinkatDir() {
  if (unlinkat(AT_FDCWD, "dir", AT_REMOVEDIR) != 0) {
    die("unlinkat dir failed");
  }
}

void testUtimes() {
  struct timeval times[] = { { 0, 0 }, { 0, 0 } };
  utimes("input", times);
}

void testVforkInheritFd() {
  testForkOrVforkInheritFd(&vfork);
}

const std::unordered_map<std::string, std::function<void ()>> kTests = {
  { "access", testAccess },
  { "accessx_np", testAccessxNp },
  { "chdir", testChdir },
  { "chdir_other_thread", testChdirOtherThread },
  { "chdir_fail", testChdirFail },
  { "chflags", testChflags },
  { "chmod", testChmod },
  { "chmod_extended", testChmodExtended },
  { "chown", testChown },
  { "chroot", testChroot },
  { "close", testClose },
  { "close_nocancel", testCloseNocancel },
  { "copyfile", testCopyfile },
  { "delete", testDelete },
  { "dup", testDup },
  { "dup2", testDup2 },
  { "exchangedata", testExchangedata },
  { "execve", testExecve },
  { "faccessat", testFaccessat },
  { "fchdir", testFchdir },
  { "fchflags", testFchflags },
  { "fchmod", testFchmod },
  { "fchmod_extended", testFchmodExtended },
  { "fchmodat", testFchmodat },
  { "fchown", testFchown },
  { "fchownat", testFchownat },
  { "fcntl_disable_cloexec", testFcntlDisableCloexec },
  { "fcntl_dupfd", testFcntlDupfd },
  { "fcntl_dupfd_cloexec", testFcntlDupfdCloexec },
  { "fcntl_dupfd_cloexec_exec", testFcntlDupfdCloexecExec },
  { "fcntl_enable_cloexec", testFcntlEnableCloexec },
  { "fcntl_nocancel_dupfd", testFcntlNocancelDupfd },
  { "fgetattrlist", testFgetattrlist },
  { "fgetxattr", testFgetxattr },
  { "fhopen", testFhopen },
  { "flistxattr", testFlistxattr },
  { "flock", testFlock },
  { "fork_inherit_fd", testForkInheritFd },
  { "fpathconf", testFpathconf },
  { "fremovexattr", testFremovexattr },
  { "fsetattrlist", testFsetattrlist },
  { "fsetxattr", testFsetxattr },
  { "fstat", testFstat },
  { "fstat_extended", testFstatExtended },
  { "fstat64", testFstat64 },
  { "fstat64_extended", testFstat64Extended },
  { "fstatat", testFstatat },
  { "futimes", testFutimes },
  { "getattrlist", testGetattrlist },
  { "getattrlistat", testGetattrlistat },
  { "getattrlistbulk", testGetattrlistbulk },
  { "getdirentries", testGetdirentries },
  { "getdirentriesattr", testGetdirentriesattr },
  { "getxattr", testGetxattr },
  { "guarded_close_np", testGuardedCloseNp },
  { "guarded_open_dprotected_np", testGuardedOpenDprotectedNp },
  { "guarded_open_np", testGuardedOpenNp },
  { "lchown", testLchown },
  { "link", testLink },
  { "linkat", testLinkat },
  { "listxattr", testListxattr },
  { "lstat", testLstat },
  { "lstat_extended", testLstatExtended },
  { "lstat64", testLstat64 },
  { "lstat64_extended", testLstat64Extended },
  { "mkdir", testMkdir },
  { "mkdir_extended", testMkdirExtended },
  { "mkdirat", testMkdirat },
  { "mkfifo", testMkfifo },
  { "mkfifo_extended", testMkfifoExtended },
  { "mknod", testMknod },
  { "open_cloexec", testOpenCloexec },
  { "open_cloexec:continuation", testOpenCloexecContinuation },
  { "open_cloexec_off", testOpenCloexecOff },
  { "open_cloexec_off:continuation", testOpenCloexecOffContinuation },
  { "open_create", testOpenCreate },
  { "open_create_and_read", testOpenCreateAndRead },
  { "open_create_excl", testOpenCreateExcl },
  { "open_create_excl_append", testOpenCreateExclAppend },
  { "open_dprotected_np", testOpenDprotectedNp },
  { "open_extended", testOpenExtended },
  { "open_implicit_read", testOpenImplicitRead },
  { "open_nocancel", testOpenNocancel },
  { "open_partial_overwrite", testOpenPartialOverwrite },
  { "open_read", testOpenRead },
  { "openat", testOpenat },
  { "openat_nocancel", testOpenatNocancel },
  { "openat_with_openat_fd", testOpenatWithOpenatFd },
  { "openbyid_np", testOpenbyidNp },
  { "opendir", testOpendir },
  { "pathconf", testPathconf },
  { "posix_spawn", testPosixSpawn },
  { "pthread_chdir", testPthreadChdir },
  { "pthread_chdir_other_thread", testPthreadChdirOtherThread },
  { "pthread_chdir_fail", testPthreadChdirFail },
  { "pthread_fchdir", testPthreadFchdir },
  { "pthread_fchdir_other_thread", testPthreadFchdirOtherThread },
  { "readlink", testReadlink },
  { "readlinkat", testReadlinkat },
  { "removexattr", testRemovexattr },
  { "rename", testRename },
  { "rename_ext", testRenameExt },
  { "renameat", testRenameat },
  { "renamex_np", testRenamexNp },
  { "renameatx_np", testRenameatxNp },
  { "rmdir", testRmdir },
  { "searchfs", testSearchfs },
  { "setattrlist", testSetattrlist },
  { "setxattr", testSetxattr },
  { "stat", testStat },
  { "stat_extended", testStatExtended },
  { "stat64", testStat64 },
  { "stat64_extended", testStat64Extended },
  { "symlink", testSymlink },
  { "symlinkat", testSymlinkat },
  { "truncate", testTruncate },
  { "undelete", testUndelete },
  { "unlink", testUnlink },
  { "unlinkat", testUnlinkat },
  { "unlinkat_dir", testUnlinkatDir },
  { "utimes", testUtimes },
  { "vfork_inherit_fd", testVforkInheritFd },
};

}  // anonymous namespace

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s [test-name]\nAvailable tests:\n", argv[0]);
    for (const auto &test : kTests) {
      printf("  %s\n", test.first.c_str());
    }
    return 1;
  }

  self_executable_path = argv[0];

  const std::string test_name = argv[1];
  auto test_it = kTests.find(test_name);
  if (test_it == kTests.end()) {
    fprintf(stderr, "No test with name %s found.\n", test_name.c_str());
    return 1;
  }

  test_it->second();

  return 0;
}
