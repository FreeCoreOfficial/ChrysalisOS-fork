#include "exec64.h"

extern "C" int execve_linux_x86_64_full(const char *filename,
                                        char *const argv[]) {
  (void)filename;
  (void)argv;
  return -1;
}
