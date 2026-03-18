#include "linux_syscall_i386.h"
#include "linux_abi.h"

#include "../include/chrysalis/syscall_nums.h"
#include "../include/task.h"
#include "../mm/paging.h"
#include "../sched/pcb.h"
#include "../string.h"
#include "../time/timer.h"
#include "../memory/pmm.h"
#include "../user/user.h"

extern "C" int syscall_dispatch_chrys(uint32_t num, uint32_t a1, uint32_t a2,
                                      uint32_t a3, uint32_t a4, uint32_t a5,
                                      uint32_t a6);
extern "C" int syscall_user_range_ok(const void *ptr, uint32_t len);

#define LINUX_EFAULT 14
#define LINUX_ENOSYS 38

/* Minimal i386 Linux syscall numbers (subset) */
#define LINUX_NR_exit 1
#define LINUX_NR_read 3
#define LINUX_NR_write 4
#define LINUX_NR_open 5
#define LINUX_NR_close 6
#define LINUX_NR_time 13
#define LINUX_NR_getpid 20
#define LINUX_NR_getuid 24
#define LINUX_NR_getgid 47
#define LINUX_NR_getppid 64
#define LINUX_NR_gettimeofday 78
#define LINUX_NR_sysinfo 116
#define LINUX_NR_uname 122
#define LINUX_NR_exit_group 252

static int linux_copy_to_user(void *dst, const void *src, uint32_t len) {
  if (!dst || !src || len == 0)
    return -LINUX_EFAULT;
  if (!syscall_user_range_ok(dst, len))
    return -LINUX_EFAULT;
  memcpy(dst, src, len);
  return 0;
}

static void linux_fill_utsname(struct linux_utsname *u) {
  if (!u)
    return;
  strncpy(u->sysname, "ChrysalisOS", sizeof(u->sysname) - 1);
  strncpy(u->nodename, "chrysalis", sizeof(u->nodename) - 1);
#ifdef CHRYVER
  strncpy(u->release, CHRYVER, sizeof(u->release) - 1);
#else
  strncpy(u->release, "chrysver-unknown", sizeof(u->release) - 1);
#endif
  strncpy(u->version, __DATE__ " " __TIME__, sizeof(u->version) - 1);
  strncpy(u->machine, "i386", sizeof(u->machine) - 1);
  strncpy(u->domainname, "(none)", sizeof(u->domainname) - 1);
  u->sysname[sizeof(u->sysname) - 1] = 0;
  u->nodename[sizeof(u->nodename) - 1] = 0;
  u->release[sizeof(u->release) - 1] = 0;
  u->version[sizeof(u->version) - 1] = 0;
  u->machine[sizeof(u->machine) - 1] = 0;
  u->domainname[sizeof(u->domainname) - 1] = 0;
}

static int linux_sys_uname(uint32_t a1) {
  struct linux_utsname u;
  linux_fill_utsname(&u);
  return linux_copy_to_user((void *)(uintptr_t)a1, &u, sizeof(u));
}

static int linux_sys_sysinfo(uint32_t a1) {
  struct linux_sysinfo info;
  memset(&info, 0, sizeof(info));

  info.uptime = (int64_t)timer_uptime_seconds();

  uint32_t total_frames = pmm_total_frames();
  uint32_t used_frames = pmm_used_frames();
  uint32_t free_frames = (total_frames > used_frames) ?
                             (total_frames - used_frames) : 0;

  info.totalram = (uint64_t)total_frames * (uint64_t)PAGE_SIZE;
  info.freeram = (uint64_t)free_frames * (uint64_t)PAGE_SIZE;
  info.sharedram = 0;
  info.bufferram = 0;
  info.totalswap = 0;
  info.freeswap = 0;
  info.procs = 1;
  info.totalhigh = 0;
  info.freehigh = 0;
  info.mem_unit = 1;

  return linux_copy_to_user((void *)(uintptr_t)a1, &info, sizeof(info));
}

static int linux_sys_gettimeofday(uint32_t a1) {
  if (a1 == 0)
    return 0;
  struct linux_timeval64 tv;
  tv.tv_sec = (int64_t)timer_uptime_seconds();
  tv.tv_usec = 0;
  return linux_copy_to_user((void *)(uintptr_t)a1, &tv, sizeof(tv));
}

static int linux_sys_time(uint32_t a1) {
  int64_t now = (int64_t)timer_uptime_seconds();
  if (a1) {
    if (!syscall_user_range_ok((void *)(uintptr_t)a1, sizeof(uint32_t)))
      return -LINUX_EFAULT;
    *(uint32_t *)(uintptr_t)a1 = (uint32_t)now;
  }
  return (int)now;
}

int linux_syscall_dispatch_i386(uint32_t num, uint32_t a1, uint32_t a2,
                                uint32_t a3, uint32_t a4, uint32_t a5,
                                uint32_t a6) {
  (void)a4;
  (void)a5;
  (void)a6;

  pcb_t *cur = pcb_get_current();

  switch (num) {
  case LINUX_NR_exit:
  case LINUX_NR_exit_group:
    return syscall_dispatch_chrys(SYS_EXIT, a1, 0, 0, 0, 0, 0);

  case LINUX_NR_read:
    return syscall_dispatch_chrys(SYS_READ, a1, a2, a3, 0, 0, 0);

  case LINUX_NR_write:
    return syscall_dispatch_chrys(SYS_WRITE, a1, a2, a3, 0, 0, 0);

  case LINUX_NR_open:
    return syscall_dispatch_chrys(SYS_OPEN, a1, a2, 0, 0, 0, 0);

  case LINUX_NR_close:
    return syscall_dispatch_chrys(SYS_CLOSE, a1, 0, 0, 0, 0, 0);

  case LINUX_NR_getpid:
    return cur ? cur->pid : 1;

  case LINUX_NR_getppid:
    return 0;

  case LINUX_NR_getuid: {
    user_t *u = user_get_current();
    return u ? (int)u->uid : 0;
  }

  case LINUX_NR_getgid: {
    user_t *u = user_get_current();
    return u ? (int)u->gid : 0;
  }

  case LINUX_NR_uname:
    return linux_sys_uname(a1);

  case LINUX_NR_sysinfo:
    return linux_sys_sysinfo(a1);

  case LINUX_NR_gettimeofday:
    return linux_sys_gettimeofday(a1);

  case LINUX_NR_time:
    return linux_sys_time(a1);

  default:
    return -LINUX_ENOSYS;
  }
}
