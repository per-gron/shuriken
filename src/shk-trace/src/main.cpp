#include <errno.h>
#include <libc.h>

#include "kdebug_controller.h"
#include "tracer.h"

extern "C" int reexec_to_match_kernel();

namespace shk {

int getNumCpus() {
  int num_cpus;
  size_t len = sizeof(num_cpus);
  static int name[] = { CTL_HW, HW_NCPU, 0 };
  if (sysctl(name, 2, &num_cpus, &len, nullptr, 0) < 0) {
    throw std::runtime_error("Failed to get number of CPUs");
  }
  return num_cpus;
}

class DummyTracerDelegate : public Tracer::Delegate {
 public:
  virtual void newThread(
      uintptr_t parent_thread_id,
      uintptr_t child_thread_id,
      int parent_pid) override {
    printf("-- New thread!\n");
  }

  virtual void terminateThread(uintptr_t thread_id) override {
    printf("-- Terminate thread!\n");
  }

  virtual void fileEvent(
      uintptr_t thread_id,
      Tracer::EventType type,
      std::string &&path) override {
    printf("-- File event!\n");
  }
};

int main(int argc, char *argv[]) {
  if (0 != reexec_to_match_kernel()) {
    fprintf(stderr, "Could not re-execute: %d\n", errno);
    exit(1);
  }

  if (geteuid() != 0) {
    fprintf(stderr, "This tool must be run as root\n");
    exit(1);
  }

  auto kdebug_ctrl = makeKdebugController();
  DummyTracerDelegate tracer_delegate;
  Tracer tracer(
      getNumCpus(),
      *kdebug_ctrl,
      tracer_delegate);
  tracer.start(dispatch_get_main_queue());

  dispatch_main();
}

}  // namespace shk


int main(int argc, char *argv[]) {
  shk::main(argc, argv);
}