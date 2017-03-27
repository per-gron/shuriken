#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <sys/types.h>
#include <sys/acl.h>
#include <sys/attr.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syslimits.h>
#include <sys/time.h>
#include <sys/xattr.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>

#include <util/file_descriptor.h>

extern "C" int __lstat_extended(
    const char *path,
    struct stat *s,
    struct kauth_filesec *sec,
    size_t *sec_size);
extern "C" int __mkfifo_extended(
    const char *, uid_t, gid_t, int, struct kauth_filesec *);
extern "C" int __mkdir_extended(
    const char *, uid_t, gid_t, int, struct kauth_filesec *);
extern "C" int __open_nocancel(const char *, int, ...);
extern "C" int __openat_nocancel(
    int fd, const char *fname, int oflag, mode_t mode);
extern "C" int __pthread_chdir(const char *path);
extern "C" int __pthread_fchdir(int fd);
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

void testChown() {
  chown("input", getuid(), getgid());
}

void testChroot() {
  // This syscall only works for root, but that's okay because it is a
  // restricted/illegal operation even if it fails.
  chroot("/");
}

void testDup() {
  auto usr_fd = openFileForReading("/usr");
  auto duped_fd = shk::FileDescriptor(dup(usr_fd.get()));
  if (duped_fd.get() == -1) {
    die("dup failed");
  }

  assert(openat(
      duped_fd.get(), "nonexisting_path_just_for_testing", O_RDONLY) == -1);
}

void testDup2() {
  auto usr_fd = openFileForReading("/usr");

  int new_fd_num = 123;
  if (dup2(usr_fd.get(), new_fd_num) == -1) {
    die("dup2 failed");
  }
  auto duped_fd = shk::FileDescriptor(new_fd_num);

  assert(openat(
      duped_fd.get(), "nonexisting_path_just_for_testing", O_RDONLY) == -1);
}

void testExchangedata() {
  exchangedata("input", "output", 0);
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

void testFchown() {
  auto input_fd = openFileForReading("input");
  fchown(input_fd.get(), getuid(), getgid());
}

void testFchownat() {
  auto dir_fd = openFileForReading("dir");
  fchownat(dir_fd.get(), "input", getuid(), getgid(), 0);
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
    assert(openat(
        usr_fd.get(), "nonexisting_path_just_for_testing", O_RDONLY) == -1);
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

void testFstatat() {
  auto dir_fd = openFileForReading("dir");
  struct stat s;
  fstatat(dir_fd.get(), "input", &s, 0);
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

void testGetxattr() {
  char buf[1024];
  getxattr("input", "test", buf, sizeof(buf), 0, 0);
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

void testOpenNocancel() {
  // Don't check for an error code; some tests trigger an error intentionally.
  shk::FileDescriptor(__open_nocancel("input", O_RDONLY, 0));
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

void testOpenbyidNp() {
  if (openbyid_np(nullptr, nullptr, 0) != -1) {
    die("openbyid_np succeeded");
  }
}

void testPathconf() {
  pathconf("input", _PC_LINK_MAX);
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
  size_t sec_size = sizeof(filesec);
  filesec.fsec_magic = KAUTH_FILESEC_MAGIC;
  struct stat s;
  __stat_extended("input", &s, &filesec, &sec_size);
}

void testStat64() {
  struct stat64 s;
  stat64("input", &s);
}

void testStat64Extended() {
  struct kauth_filesec filesec{ 0 };
  size_t sec_size = sizeof(filesec);
  filesec.fsec_magic = KAUTH_FILESEC_MAGIC;
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
  { "chdir", testChdir },
  { "chdir_other_thread", testChdirOtherThread },
  { "chdir_fail", testChdirFail },
  { "chflags", testChflags },
  { "chmod", testChmod },
  { "chown", testChown },
  { "chroot", testChroot },
  { "dup", testDup },
  { "dup2", testDup2 },
  { "exchangedata", testExchangedata },
  { "faccessat", testFaccessat },
  { "fchdir", testFchdir },
  { "fchflags", testFchflags },
  { "fchmod", testFchmod },
  { "fchmodat", testFchmodat },
  { "fchown", testFchown },
  { "fchownat", testFchownat },
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
  { "fstat64", testFstat64 },
  { "fstatat", testFstatat },
  { "futimes", testFutimes },
  { "getattrlist", testGetattrlist },
  { "getattrlistat", testGetattrlistat },
  { "getxattr", testGetxattr },
  { "lchown", testLchown },
  { "link", testLink },
  { "linkat", testLinkat },
  { "listxattr", testListxattr },
  { "lstat", testLstat },
  { "lstat_extended", testLstatExtended },
  { "lstat64", testLstat64 },
  { "mkdir", testMkdir },
  { "mkdir_extended", testMkdirExtended },
  { "mkdirat", testMkdirat },
  { "mkfifo", testMkfifo },
  { "mkfifo_extended", testMkfifoExtended },
  { "mknod", testMknod },
  { "open_nocancel", testOpenNocancel },
  { "openat", testOpenat },
  { "openat_nocancel", testOpenatNocancel },
  { "openbyid_np", testOpenbyidNp },
  { "pathconf", testPathconf },
  { "pthread_chdir", testPthreadChdir },
  { "pthread_chdir_other_thread", testPthreadChdirOtherThread },
  { "pthread_chdir_fail", testPthreadChdirFail },
  { "pthread_fchdir", testPthreadFchdir },
  { "pthread_fchdir_other_thread", testPthreadFchdirOtherThread },
  { "readlink", testReadlink },
  { "readlinkat", testReadlinkat },
  { "removexattr", testRemovexattr },
  { "rename", testRename },
  { "renameat", testRenameat },
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

  const std::string test_name = argv[1];
  auto test_it = kTests.find(test_name);
  if (test_it == kTests.end()) {
    fprintf(stderr, "No test with name %s found.\n", test_name.c_str());
    return 1;
  }

  test_it->second();

  return 0;
}
