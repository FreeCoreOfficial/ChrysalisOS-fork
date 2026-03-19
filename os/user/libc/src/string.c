#include "../include/string.h"

void *memcpy(void *dest, const void *src, size_t n) {
  unsigned char *d = (unsigned char *)dest;
  const unsigned char *s = (const unsigned char *)src;
  for (size_t i = 0; i < n; i++)
    d[i] = s[i];
  return dest;
}

void *memset(void *s, int c, size_t n) {
  unsigned char *p = (unsigned char *)s;
  for (size_t i = 0; i < n; i++)
    p[i] = (unsigned char)c;
  return s;
}

void *memmove(void *dest, const void *src, size_t n) {
  unsigned char *d = (unsigned char *)dest;
  const unsigned char *s = (const unsigned char *)src;
  if (d == s || n == 0)
    return dest;
  if (d < s) {
    for (size_t i = 0; i < n; i++)
      d[i] = s[i];
  } else {
    for (size_t i = n; i > 0; i--)
      d[i - 1] = s[i - 1];
  }
  return dest;
}

size_t strlen(const char *s) {
  size_t len = 0;
  while (s && s[len])
    len++;
  return len;
}

int strcmp(const char *a, const char *b) {
  if (a == b)
    return 0;
  if (!a)
    return -1;
  if (!b)
    return 1;
  while (*a && (*a == *b)) {
    a++;
    b++;
  }
  return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
  if (n == 0)
    return 0;
  if (a == b)
    return 0;
  if (!a)
    return -1;
  if (!b)
    return 1;
  while (n-- > 1 && *a && (*a == *b)) {
    a++;
    b++;
  }
  return (unsigned char)*a - (unsigned char)*b;
}

char *strcpy(char *dest, const char *src) {
  char *d = dest;
  if (!dest || !src)
    return dest;
  while ((*d++ = *src++))
    ;
  return dest;
}

char *strncpy(char *dest, const char *src, size_t n) {
  size_t i = 0;
  if (!dest || !src)
    return dest;
  for (; i < n && src[i]; i++)
    dest[i] = src[i];
  for (; i < n; i++)
    dest[i] = 0;
  return dest;
}

char *strchr(const char *s, int c) {
  if (!s)
    return 0;
  for (; *s; s++) {
    if (*s == (char)c)
      return (char *)s;
  }
  if (c == 0)
    return (char *)s;
  return 0;
}
