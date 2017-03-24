#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <sys/syslimits.h>
#include <unistd.h>
#include <unordered_map>

#include <util/file_descriptor.h>

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

void testForkInheritFd() {
  // Verify that file descriptors are inherited

  auto usr_fd = openFileForReading("/usr");

  pid_t pid = fork();
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

void testReadlink() {
  char buf[1024];
  // Don't check for an error code; some tests trigger an error intentionally.
  readlink("input", buf, sizeof(buf));
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

const std::unordered_map<std::string, std::function<void ()>> kTests = {
  { "access", testAccess },
  { "fork_inherit_fd", testForkInheritFd },
  { "link", testLink },
  { "linkat", testLinkat },
  { "readlink", testReadlink },
  { "unlink", testUnlink },
  { "unlinkat", testUnlinkat },
  { "unlinkat_dir", testUnlinkatDir },
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
