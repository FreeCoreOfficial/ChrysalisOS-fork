#include "../include/unistd.h"
#include "../../../kernel/include/chrysalis/syscall_nums.h"

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

ssize_t write(int fd, const void *buf, size_t count) {
  return (ssize_t)syscall3(SYS_WRITE, (uint32_t)fd,
                           (uint32_t)(uintptr_t)buf, (uint32_t)count);
}

ssize_t read(int fd, void *buf, size_t count) {
  return (ssize_t)syscall3(SYS_READ, (uint32_t)fd,
                           (uint32_t)(uintptr_t)buf, (uint32_t)count);
}

int open(const char *path, int flags) {
  return syscall2(SYS_OPEN, (uint32_t)(uintptr_t)path, (uint32_t)flags);
}

int close(int fd) { return syscall1(SYS_CLOSE, (uint32_t)fd); }

int ioctl(int fd, uint32_t cmd, void *arg) {
  return syscall3(SYS_IOCTL, (uint32_t)fd, cmd, (uint32_t)(uintptr_t)arg);
}

void _exit(int code) { syscall1(SYS_EXIT, (uint32_t)code); }
