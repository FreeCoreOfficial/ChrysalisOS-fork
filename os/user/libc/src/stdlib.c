#include "../include/stdlib.h"
#include "../include/string.h"
#include "../include/errno.h"

typedef struct {
  size_t size;
} libc_block_t;

static unsigned char g_heap[256 * 1024];
static size_t g_heap_off = 0;

static size_t align_up(size_t v, size_t a) {
  return (v + (a - 1)) & ~(a - 1);
}

void *malloc(size_t size) {
  if (size == 0)
    return 0;
  size = align_up(size, 8);
  size_t total = size + sizeof(libc_block_t);
  if (g_heap_off + total > sizeof(g_heap)) {
    errno = 12;
    return 0;
  }
  libc_block_t *blk = (libc_block_t *)&g_heap[g_heap_off];
  blk->size = size;
  g_heap_off += total;
  return (void *)(blk + 1);
}

void free(void *ptr) { (void)ptr; }

void *calloc(size_t nmemb, size_t size) {
  size_t total = nmemb * size;
  void *p = malloc(total);
  if (p)
    memset(p, 0, total);
  return p;
}

void *realloc(void *ptr, size_t size) {
  if (!ptr)
    return malloc(size);
  if (size == 0)
    return 0;
  libc_block_t *blk = ((libc_block_t *)ptr) - 1;
  size_t copy = blk->size < size ? blk->size : size;
  void *n = malloc(size);
  if (!n)
    return 0;
  memcpy(n, ptr, copy);
  return n;
}

int atoi(const char *s) {
  if (!s)
    return 0;
  int sign = 1;
  if (*s == '-') {
    sign = -1;
    s++;
  } else if (*s == '+') {
    s++;
  }
  int v = 0;
  while (*s >= '0' && *s <= '9') {
    v = v * 10 + (*s - '0');
    s++;
  }
  return v * sign;
}
