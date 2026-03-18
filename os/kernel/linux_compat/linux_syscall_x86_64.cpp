#include "linux_syscall_x86_64.h"
#include "linux_abi.h"

#include "../include/chrysalis/syscall_nums.h"
#include "../include/task.h"
#include "../terminal.h"

#define LINUX_ENOSYS 38

int linux_syscall_dispatch_x86_64(uint32_t num, uint32_t a1, uint32_t a2,
                                  uint32_t a3, uint32_t a4, uint32_t a5,
                                  uint32_t a6) {
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  terminal_printf("[linux64] syscall %u not supported on 32-bit kernel\n", num);
  return -LINUX_ENOSYS;
}
