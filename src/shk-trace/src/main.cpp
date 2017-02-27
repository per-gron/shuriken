#include <errno.h>
#include <libc.h>

#include "tracer.h"

extern "C" int reexec_to_match_kernel();

int main(int argc, char *argv[]) {
  if (0 != reexec_to_match_kernel()) {
    fprintf(stderr, "Could not re-execute: %d\n", errno);
    exit(1);
  }

  if (geteuid() != 0) {
    fprintf(stderr, "This tool must be run as root\n");
    exit(1);
  }

  shk::Tracer().run();
}
