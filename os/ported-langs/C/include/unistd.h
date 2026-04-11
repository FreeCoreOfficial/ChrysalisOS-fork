#ifndef CHRYSALIS_LIBC_UNISTD_H
#define CHRYSALIS_LIBC_UNISTD_H

#include <stdint.h>
#include <stddef.h>

typedef long ssize_t;

ssize_t write(int fd, const void *buf, size_t count);
ssize_t read(int fd, void *buf, size_t count);
int open(const char *path, int flags);
int close(int fd);
int ioctl(int fd, uint32_t cmd, void *arg);
int readlink(const char *path, char *buf, size_t bufsize);
void _exit(int code);


#endif
