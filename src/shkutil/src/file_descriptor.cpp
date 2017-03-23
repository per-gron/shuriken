#include <util/file_descriptor.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace shk {
namespace detail {

void closeFd(int fd) {
  auto result = close(fd);
  if (result != 0) {
    fprintf(stderr, "Failed to close fd: %s\n", strerror(errno));
  }
}

}
}
