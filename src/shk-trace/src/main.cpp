#include <errno.h>
#include <libc.h>

#include "kdebug_controller.h"
#include "tracer.h"

extern "C" int reexec_to_match_kernel();

namespace {

int getNumCpus() {
  int num_cpus;
  size_t len = sizeof(num_cpus);
  static int name[] = { CTL_HW, HW_NCPU, 0 };
  if (sysctl(name, 2, &num_cpus, &len, nullptr, 0) < 0) {
    throw std::runtime_error("Failed to get number of CPUs");
  }
  return num_cpus;
}

}  // anonymous namespace

int main(int argc, char *argv[]) {
  if (0 != reexec_to_match_kernel()) {
    fprintf(stderr, "Could not re-execute: %d\n", errno);
    exit(1);
  }

  if (geteuid() != 0) {
    fprintf(stderr, "This tool must be run as root\n");
    exit(1);
  }

  shk::Tracer tracer(getNumCpus(), shk::makeKdebugController());
  tracer.start(dispatch_get_main_queue());

  dispatch_main();
}
