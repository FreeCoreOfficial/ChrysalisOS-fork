#include "../include/stdio.h"
#include "../include/string.h"
#include "../include/unistd.h"
#include <stdbool.h>

static int write_all(const char *buf, size_t len) {
  if (!buf || len == 0)
    return 0;
  ssize_t r = write(1, buf, len);
  if (r < 0)
    return 0;
  return (int)r;
}

static int emit_char(char c) { return write_all(&c, 1); }

static int emit_str(const char *s) {
  if (!s)
    s = "(null)";
  return write_all(s, strlen(s));
}

static int emit_uint(unsigned int v, unsigned int base, int uppercase) {
  char buf[32];
  int i = 0;

  if (base < 2 || base > 16)
    return 0;

  do {
    unsigned int digit = v % base;
    if (digit < 10)
      buf[i++] = (char)('0' + digit);
    else
      buf[i++] = (char)((uppercase ? 'A' : 'a') + (digit - 10));
    v /= base;
  } while (v != 0 && i < (int)sizeof(buf));

  int written = 0;
  for (int j = i - 1; j >= 0; j--) {
    written += emit_char(buf[j]);
  }
  return written;
}

static int emit_int(int v) {
  unsigned int uv;
  int written = 0;

  if (v < 0) {
    written += emit_char('-');
    uv = (unsigned int)(-(v + 1)) + 1;
  } else {
    uv = (unsigned int)v;
  }

  written += emit_uint(uv, 10, false);
  return written;
}

int vprintf(const char *fmt, va_list ap) {
  if (!fmt)
    return 0;

  int written = 0;
  for (const char *p = fmt; *p; p++) {
    if (*p != '%') {
      written += emit_char(*p);
      continue;
    }

    p++;
    if (!*p)
      break;

    switch (*p) {
    case '%':
      written += emit_char('%');
      break;
    case 'c': {
      int ch = va_arg(ap, int);
      written += emit_char((char)ch);
      break;
    }
    case 's': {
      const char *s = va_arg(ap, const char *);
      written += emit_str(s);
      break;
    }
    case 'd':
    case 'i': {
      int v = va_arg(ap, int);
      written += emit_int(v);
      break;
    }
    case 'u': {
      unsigned int v = va_arg(ap, unsigned int);
      written += emit_uint(v, 10, false);
      break;
    }
    case 'x': {
      unsigned int v = va_arg(ap, unsigned int);
      written += emit_uint(v, 16, false);
      break;
    }
    case 'X': {
      unsigned int v = va_arg(ap, unsigned int);
      written += emit_uint(v, 16, true);
      break;
    }
    default:
      written += emit_char('%');
      written += emit_char(*p);
      break;
    }
  }

  return written;
}

int printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vprintf(fmt, ap);
  va_end(ap);
  return r;
}
