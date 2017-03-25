#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <sys/syslimits.h>
#include <sys/time.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>

#include <util/file_descriptor.h>

extern "C" int __pthread_chdir(const char *path);
extern "C" int __pthread_fchdir(int fd);

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

void testMkdir() {
  // Don't check for an error code; some tests trigger an error intentionally.
  mkdir("output", 0666);
}

void testMkdirat() {
  auto dir_fd = openFileForReading("dir");
  // Don't check for an error code; some tests trigger an error intentionally.
  mkdirat(dir_fd.get(), "output", 0666);
}

void testMkfifo() {
  // Don't check for an error code; some tests trigger an error intentionally.
  mkfifo("output", 0666);
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
  { "dup", testDup },
  { "dup2", testDup2 },
  { "faccessat", testFaccessat },
  { "fchdir", testFchdir },
  { "fchflags", testFchflags },
  { "fork_inherit_fd", testForkInheritFd },
  { "link", testLink },
  { "linkat", testLinkat },
  { "mkdir", testMkdir },
  { "mkdirat", testMkdirat },
  { "mkfifo", testMkfifo },
  { "pthread_chdir", testPthreadChdir },
  { "pthread_chdir_other_thread", testPthreadChdirOtherThread },
  { "pthread_chdir_fail", testPthreadChdirFail },
  { "pthread_fchdir", testPthreadFchdir },
  { "pthread_fchdir_other_thread", testPthreadFchdirOtherThread },
  { "readlink", testReadlink },
  { "readlinkat", testReadlinkat },
  { "rename", testRename },
  { "renameat", testRenameat },
  { "symlink", testSymlink },
  { "symlinkat", testSymlinkat },
  { "truncate", testTruncate },
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
