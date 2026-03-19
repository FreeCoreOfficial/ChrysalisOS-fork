#include "linux_syscall_x86_64.h"
#include "linux_abi.h"

#include "../include/chrysalis/syscall_nums.h"
#include "../include/task.h"
#include "../string.h"
#include "../time/clock.h"
#include "../mem/user64_vm.h"

#define LINUX_EFAULT 14
#define LINUX_EINVAL 22
#define LINUX_ENOSYS 38
#define LINUX_ENOMEM 12

#if defined(__x86_64__)
extern "C" uint64_t syscall64_dispatch(uint64_t num, uint64_t a1, uint64_t a2,
                                       uint64_t a3, uint64_t a4, uint64_t a5,
                                       uint64_t a6);

/* Minimal x86_64 Linux syscall numbers (subset) */
#define LINUX_NR_read 0
#define LINUX_NR_write 1
#define LINUX_NR_open 2
#define LINUX_NR_close 3
#define LINUX_NR_mmap 9
#define LINUX_NR_mprotect 10
#define LINUX_NR_munmap 11
#define LINUX_NR_brk 12
#define LINUX_NR_sched_yield 24
#define LINUX_NR_nanosleep 35
#define LINUX_NR_getpid 39
#define LINUX_NR_uname 63
#define LINUX_NR_gettimeofday 96
#define LINUX_NR_sysinfo 99
#define LINUX_NR_getuid 102
#define LINUX_NR_getgid 104
#define LINUX_NR_getppid 110
#define LINUX_NR_time 201
#define LINUX_NR_exit 60
#define LINUX_NR_exit_group 231
#define LINUX_NR_openat 257

static int linux_copy_to_user(void *dst, const void *src, uint32_t len) {
  if (!dst || !src || len == 0)
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
  strncpy(u->machine, "x86_64", sizeof(u->machine) - 1);
  strncpy(u->domainname, "(none)", sizeof(u->domainname) - 1);
  u->sysname[sizeof(u->sysname) - 1] = 0;
  u->nodename[sizeof(u->nodename) - 1] = 0;
  u->release[sizeof(u->release) - 1] = 0;
  u->version[sizeof(u->version) - 1] = 0;
  u->machine[sizeof(u->machine) - 1] = 0;
  u->domainname[sizeof(u->domainname) - 1] = 0;
}

static int linux_sys_uname(uint64_t a1) {
  struct linux_utsname u;
  linux_fill_utsname(&u);
  return linux_copy_to_user((void *)(uintptr_t)a1, &u, sizeof(u));
}

static uint64_t days_before_year(int year) {
  uint64_t y = (uint64_t)year;
  uint64_t y_prev = y - 1;
  return 365ULL * y_prev + y_prev / 4 - y_prev / 100 + y_prev / 400;
}

static uint64_t days_before_month(int year, int month) {
  static const int days_norm[] = {0, 31, 59, 90, 120, 151, 181,
                                  212, 243, 273, 304, 334};
  uint64_t days = days_norm[month - 1];
  bool leap = ((year % 4) == 0 && (year % 100) != 0) || (year % 400) == 0;
  if (leap && month > 2)
    days++;
  return days;
}

static uint64_t linux_epoch_from_datetime(const struct datetime *t) {
  if (!t || t->year < 1970 || t->month < 1 || t->month > 12 || t->day < 1)
    return 0;
  uint64_t days_1970 = days_before_year(1970);
  uint64_t days = days_before_year(t->year) - days_1970;
  days += days_before_month(t->year, t->month);
  days += (uint64_t)(t->day - 1);
  uint64_t sec = days * 86400ULL;
  sec += (uint64_t)t->hour * 3600ULL;
  sec += (uint64_t)t->minute * 60ULL;
  sec += (uint64_t)t->second;
  return sec;
}

static int linux_sys_gettimeofday(uint64_t a1) {
  if (a1 == 0)
    return 0;
  struct datetime t;
  time_get_utc(&t);
  struct linux_timeval64 tv;
  tv.tv_sec = (int64_t)linux_epoch_from_datetime(&t);
  tv.tv_usec = 0;
  return linux_copy_to_user((void *)(uintptr_t)a1, &tv, sizeof(tv));
}

static int linux_sys_time(uint64_t a1) {
  struct datetime t;
  time_get_utc(&t);
  uint64_t now = linux_epoch_from_datetime(&t);
  if (a1) {
    *(uint64_t *)(uintptr_t)a1 = now;
  }
  return (int)now;
}

static int linux_sys_sysinfo(uint64_t a1) {
  if (!a1)
    return -LINUX_EFAULT;
  struct linux_sysinfo info;
  memset(&info, 0, sizeof(info));
  info.uptime = 0;
  info.mem_unit = 1;
  return linux_copy_to_user((void *)(uintptr_t)a1, &info, sizeof(info));
}

static int linux_sys_nanosleep(uint64_t a1) {
  if (!a1)
    return -LINUX_EFAULT;
  struct linux_timespec64 req;
  memcpy(&req, (const void *)(uintptr_t)a1, sizeof(req));
  if (req.tv_sec < 0 || req.tv_nsec < 0)
    return -LINUX_EINVAL;
  uint64_t ms = (uint64_t)req.tv_sec * 1000ULL +
                (uint64_t)req.tv_nsec / 1000000ULL;
  syscall64_dispatch(SYS_SLEEP, ms, 0, 0, 0, 0, 0);
  return 0;
}

int linux_syscall_dispatch_x86_64(uint64_t num, uint64_t a1, uint64_t a2,
                                  uint64_t a3, uint64_t a4, uint64_t a5,
                                  uint64_t a6) {
  (void)a4;
  (void)a5;
  (void)a6;

  switch (num) {
  case LINUX_NR_exit:
  case LINUX_NR_exit_group:
    return (int)syscall64_dispatch(SYS_EXIT, a1, 0, 0, 0, 0, 0);

  case LINUX_NR_read:
    return (int)syscall64_dispatch(SYS_READ, a1, a2, a3, 0, 0, 0);

  case LINUX_NR_write:
    return (int)syscall64_dispatch(SYS_WRITE, a1, a2, a3, 0, 0, 0);

  case LINUX_NR_open:
    return (int)syscall64_dispatch(SYS_OPEN, a1, a2, 0, 0, 0, 0);

  case LINUX_NR_openat:
    return (int)syscall64_dispatch(SYS_OPEN, a2, a3, 0, 0, 0, 0);

  case LINUX_NR_close:
    return (int)syscall64_dispatch(SYS_CLOSE, a1, 0, 0, 0, 0, 0);

  case LINUX_NR_brk: {
    uint64_t res = user64_brk(a1);
    return (int)res;
  }

  case LINUX_NR_mmap: {
    uint64_t addr = user64_mmap(a1, a2, (int)a3, (int)a4);
    if ((int64_t)addr < 0)
      return -LINUX_ENOMEM;
    return (int)addr;
  }

  case LINUX_NR_munmap:
    if (user64_munmap(a1, a2) < 0)
      return -LINUX_ENOMEM;
    return 0;

  case LINUX_NR_mprotect:
    return 0;

  case LINUX_NR_sched_yield:
    return (int)syscall64_dispatch(SYS_YIELD, 0, 0, 0, 0, 0, 0);

  case LINUX_NR_nanosleep:
    return linux_sys_nanosleep(a1);

  case LINUX_NR_getpid:
    return 1;

  case LINUX_NR_getppid:
    return 0;

  case LINUX_NR_getuid:
  case LINUX_NR_getgid:
    return 0;

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

#else

int linux_syscall_dispatch_x86_64(uint64_t num, uint64_t a1, uint64_t a2,
                                  uint64_t a3, uint64_t a4, uint64_t a5,
                                  uint64_t a6) {
  (void)num;
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return -LINUX_ENOSYS;
}

#endif
