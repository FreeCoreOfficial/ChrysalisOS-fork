#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum linux_abi {
  LINUX_ABI_I386 = 0,
  LINUX_ABI_X86_64 = 1,
};

struct linux_i386_syscall_regs {
  uint32_t eax;
  uint32_t ebx;
  uint32_t ecx;
  uint32_t edx;
  uint32_t esi;
  uint32_t edi;
  uint32_t ebp;
};

struct linux_x86_64_syscall_regs {
  uint64_t rax;
  uint64_t rdi;
  uint64_t rsi;
  uint64_t rdx;
  uint64_t r10;
  uint64_t r8;
  uint64_t r9;
};

struct linux_utsname {
  char sysname[65];
  char nodename[65];
  char release[65];
  char version[65];
  char machine[65];
  char domainname[65];
};

struct linux_timespec64 {
  int64_t tv_sec;
  int64_t tv_nsec;
};

struct linux_timeval64 {
  int64_t tv_sec;
  int64_t tv_usec;
};

struct linux_sysinfo {
  int64_t uptime;
  uint64_t loads[3];
  uint64_t totalram;
  uint64_t freeram;
  uint64_t sharedram;
  uint64_t bufferram;
  uint64_t totalswap;
  uint64_t freeswap;
  uint16_t procs;
  uint16_t pad;
  uint64_t totalhigh;
  uint64_t freehigh;
  uint32_t mem_unit;
  char _f[8];
};

#ifdef __cplusplus
}
#endif
