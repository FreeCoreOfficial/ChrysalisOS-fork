#include "user64_vm.h"
#include "../arch/x86_64/paging64.h"
#include "../sched/task64.h"
#include "../string.h"

#define USER64_PAGE_SIZE 0x1000ULL
#define USER64_HEAP_BASE 0x40000000ULL
#define USER64_MMAP_BASE 0x60000000ULL

static uint64_t align_up(uint64_t v) {
  return (v + USER64_PAGE_SIZE - 1) & ~(USER64_PAGE_SIZE - 1);
}

void user64_init_process(uint64_t image_end) {
  task64_t *t = task64_current();
  if (!t)
    return;
  uint64_t base = image_end;
  if (base < USER64_HEAP_BASE)
    base = USER64_HEAP_BASE;
  t->user_brk_start = base;
  t->user_brk_end = base;
  t->user_mmap_base = USER64_MMAP_BASE;
}

uint64_t user64_brk(uint64_t new_brk) {
  task64_t *t = task64_current();
  if (!t)
    return 0;
  if (new_brk == 0)
    return t->user_brk_end;
  if (new_brk < t->user_brk_start)
    return t->user_brk_end;

  uint64_t old = t->user_brk_end;
  uint64_t start = align_up(old);
  uint64_t end = align_up(new_brk);
  for (uint64_t va = start; va < end; va += USER64_PAGE_SIZE) {
    uint64_t phys = paging64_alloc_frame();
    if (!phys)
      return old;
    if (paging64_map_page(va, phys, 0x7) < 0)
      return old;
    memset((void *)(uintptr_t)phys, 0, USER64_PAGE_SIZE);
  }
  t->user_brk_end = new_brk;
  return t->user_brk_end;
}

uint64_t user64_mmap(uint64_t addr, uint64_t len, int prot, int flags) {
  (void)prot;
  (void)flags;
  task64_t *t = task64_current();
  if (!t || len == 0)
    return (uint64_t)-1;

  uint64_t size = align_up(len);
  if (addr == 0) {
    addr = align_up(t->user_mmap_base);
  } else {
    if (addr & (USER64_PAGE_SIZE - 1))
      return (uint64_t)-1;
  }

  for (uint64_t va = addr; va < addr + size; va += USER64_PAGE_SIZE) {
    uint64_t phys = paging64_alloc_frame();
    if (!phys)
      return (uint64_t)-1;
    if (paging64_map_page(va, phys, 0x7) < 0)
      return (uint64_t)-1;
    memset((void *)(uintptr_t)phys, 0, USER64_PAGE_SIZE);
  }

  if (addr + size > t->user_mmap_base)
    t->user_mmap_base = addr + size;
  return addr;
}

int user64_munmap(uint64_t addr, uint64_t len) {
  (void)addr;
  (void)len;
  return 0;
}
