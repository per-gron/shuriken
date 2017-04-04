#include <cstdio>
#include <signal.h>
#include <string.h>
#include <unistd.h>

/**
 * Executable that is used by a unit test that doesn't want to invoke the real
 * shk-trace server.
 */
int main() {
  printf("serving\n");
  fflush(stdout);
  close(1);
  usleep(100000000);
  return 0;
}
