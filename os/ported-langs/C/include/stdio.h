#ifndef CHRYSALIS_LIBC_STDIO_H
#define CHRYSALIS_LIBC_STDIO_H

#include <stddef.h>
#include "stdarg.h"

int printf(const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);

#endif
