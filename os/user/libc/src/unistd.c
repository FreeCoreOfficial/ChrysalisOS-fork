#include "../include/unistd.h"
#include "../../../kernel/include/chrysalis/syscall_nums.h"

#ifdef __x86_64__
static inline long syscall0(long num) {
  long ret;
  asm volatile("syscall" : "=a"(ret) : "a"(num) : "rcx", "r11", "memory");
  return ret;
}

static inline long syscall1(long num, long a1) {
  long ret;
  asm volatile("syscall" : "=a"(ret) : "a"(num), "D"(a1) : "rcx", "r11", "memory");
  return ret;
}

static inline long syscall2(long num, long a1, long a2) {
  long ret;
  asm volatile("syscall" : "=a"(ret) : "a"(num), "D"(a1), "S"(a2) : "rcx", "r11", "memory");
  return ret;
}

static inline long syscall3(long num, long a1, long a2, long a3) {
  long ret;
  asm volatile("syscall" : "=a"(ret) : "a"(num), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "memory");
  return ret;
}
#else
static inline int syscall0(int num) {
  int ret;
  asm volatile("int $0x80" : "=a"(ret) : "a"(num));
  return ret;
}

static inline int syscall1(int num, uint32_t a1) {
  int ret;
  asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a1));
  return ret;
}

static inline int syscall2(int num, uint32_t a1, uint32_t a2) {
  int ret;
  asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a1), "c"(a2));
  return ret;
}

static inline int syscall3(int num, uint32_t a1, uint32_t a2, uint32_t a3) {
  int ret;
  asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a1), "c"(a2), "d"(a3));
  return ret;
}
#endif

ssize_t write(int fd, const void *buf, size_t count) {
#ifdef __x86_64__
  return (ssize_t)syscall3(SYS_WRITE, fd, (uintptr_t)buf, count);
#else
  return (ssize_t)syscall3(SYS_WRITE, (uint32_t)fd,
                           (uint32_t)(uintptr_t)buf, (uint32_t)count);
#endif
}

ssize_t read(int fd, void *buf, size_t count) {
#ifdef __x86_64__
  return (ssize_t)syscall3(SYS_READ, fd, (uintptr_t)buf, count);
#else
  return (ssize_t)syscall3(SYS_READ, (uint32_t)fd,
                           (uint32_t)(uintptr_t)buf, (uint32_t)count);
#endif
}

int open(const char *path, int flags) {
#ifdef __x86_64__
  return (int)syscall2(SYS_OPEN, (uintptr_t)path, flags);
#else
  return syscall2(SYS_OPEN, (uint32_t)(uintptr_t)path, (uint32_t)flags);
#endif
}

int close(int fd) {
#ifdef __x86_64__
  return (int)syscall1(SYS_CLOSE, fd);
#else
  return syscall1(SYS_CLOSE, (uint32_t)fd);
#endif
}

int ioctl(int fd, uint32_t cmd, void *arg) {
#ifdef __x86_64__
  return (int)syscall3(SYS_IOCTL, fd, cmd, (uintptr_t)arg);
#else
  return syscall3(SYS_IOCTL, (uint32_t)fd, cmd, (uint32_t)(uintptr_t)arg);
#endif
}

int readlink(const char *path, char *buf, size_t bufsize) {
#ifdef __x86_64__
  return (int)syscall3(SYS_READLINK, (uintptr_t)path, (uintptr_t)buf, bufsize);
#else
  return -1;
#endif
}

void _exit(int code) {
#ifdef __x86_64__
  syscall1(SYS_EXIT, code);
#else
  syscall1(SYS_EXIT, (uint32_t)code);
#endif
}


