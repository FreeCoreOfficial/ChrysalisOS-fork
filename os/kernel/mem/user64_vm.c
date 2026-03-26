#include "user64_vm.h"
#include "../arch/x86_64/paging64.h"
#include "../sched/task64.h"
#include "../string.h"
#include "../drivers/serial.h"

#define USER64_PAGE_SIZE 0x1000ULL
#define USER64_HEAP_BASE 0x40000000ULL
#define USER64_MMAP_BASE 0x60000000ULL

#define USER64_PROT_WRITE 0x2
#define USER64_MAP_FIXED 0x10

#define USER64_VMA_MAX 128
#define USER64_SIGTRAMP_ADDR 0x000000007fff0000ULL

static uint64_t align_up(uint64_t v) {
  return (v + USER64_PAGE_SIZE - 1) & ~(USER64_PAGE_SIZE - 1);
}

static uint64_t align_down(uint64_t v) { return v & ~(USER64_PAGE_SIZE - 1); }

static int vma_overlap(task64_t *t, uint64_t start, uint64_t end) {
  for (int i = 0; i < USER64_VMA_MAX; i++) {
    if (!t->vmas[i].used)
      continue;
    if (end <= t->vmas[i].start || start >= t->vmas[i].end)
      continue;
    return 1;
  }
  return 0;
}

static int g_sigtramp_ready = 0;

static int vma_add(task64_t *t, uint64_t start, uint64_t end, int prot,
                   int flags) {
  for (int i = 0; i < USER64_VMA_MAX; i++) {
    if (!t->vmas[i].used) {
      t->vmas[i].used = 1;
      t->vmas[i].start = start;
      t->vmas[i].end = end;
      t->vmas[i].prot = prot;
      t->vmas[i].flags = flags;
      return 0;
    }
  }
  serial_write_string("[VMA] Error: VMA table full!\r\n");
  return -1;
}

static void vma_remove_range(task64_t *t, uint64_t start, uint64_t end) {
  for (int i = 0; i < USER64_VMA_MAX; i++) {
    if (!t->vmas[i].used)
      continue;
    uint64_t vs = t->vmas[i].start;
    uint64_t ve = t->vmas[i].end;
    if (end <= vs || start >= ve)
      continue;
    if (start <= vs && end >= ve) {
      t->vmas[i].used = 0;
      continue;
    }
    if (start > vs && end < ve) {
      uint64_t old_end = ve;
      t->vmas[i].end = start;
      vma_add(t, end, old_end, t->vmas[i].prot, t->vmas[i].flags);
      continue;
    }
    if (start <= vs && end < ve) {
      t->vmas[i].start = end;
      continue;
    }
    if (start > vs && end >= ve) {
      t->vmas[i].end = start;
      continue;
    }
  }
}

static void vma_update_prot(task64_t *t, uint64_t start, uint64_t end,
                            int prot) {
  for (int i = 0; i < USER64_VMA_MAX; i++) {
    if (!t->vmas[i].used)
      continue;
    uint64_t vs = t->vmas[i].start;
    uint64_t ve = t->vmas[i].end;
    if (end <= vs || start >= ve)
      continue;
    if (start <= vs && end >= ve) {
      t->vmas[i].prot = prot;
      continue;
    }
    if (start > vs && end < ve) {
      uint64_t old_end = ve;
      int old_prot = t->vmas[i].prot;
      int old_flags = t->vmas[i].flags;
      t->vmas[i].end = start;
      vma_add(t, start, end, prot, old_flags);
      vma_add(t, end, old_end, old_prot, old_flags);
      continue;
    }
    if (start <= vs && end < ve) {
      t->vmas[i].start = end;
      vma_add(t, vs, end, prot, t->vmas[i].flags);
      continue;
    }
    if (start > vs && end >= ve) {
      t->vmas[i].end = start;
      vma_add(t, start, ve, prot, t->vmas[i].flags);
      continue;
    }
  }
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
  for (int i = 0; i < USER64_VMA_MAX; i++)
    t->vmas[i].used = 0;
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
  if (old == 0) old = t->user_brk_start;

  if (new_brk < old) {
    uint64_t start = align_up(new_brk);
    uint64_t end = align_up(old);
    if (start < end) {
      for (uint64_t va = start; va < end; va += USER64_PAGE_SIZE) {
        paging64_unmap_page(va);
      }
    }
    t->user_brk_end = new_brk;
    return t->user_brk_end;
  }

  uint64_t start = align_up(old);
  uint64_t end = align_up(new_brk);
  for (uint64_t va = start; va < end; va += USER64_PAGE_SIZE) {
    uint64_t phys = paging64_alloc_frame();
    if (!phys)
      return old;
    if (paging64_map_page(va, phys, 0x7) < 0)
      return old;
    memset((void *)(uintptr_t)va, 0, USER64_PAGE_SIZE);
  }
  t->user_brk_end = new_brk;
  return t->user_brk_end;
}


uint64_t user64_mmap(uint64_t addr, uint64_t len, int prot, int flags) {
  task64_t *t = task64_current();
  if (!t || len == 0)
    return (uint64_t)-1;

  uint64_t size = align_up(len);
  if (addr == 0) {
    addr = align_up(t->user_mmap_base);
    int guard = 0;
    while (vma_overlap(t, addr, addr + size) && guard++ < 64) {
      addr += size;
    }
  } else {
    if (addr & (USER64_PAGE_SIZE - 1))
      return (uint64_t)-1;
    if (!(flags & USER64_MAP_FIXED))
      addr = align_up(addr);
  }

  if (flags & USER64_MAP_FIXED) {
    for (uint64_t va = addr; va < addr + size; va += USER64_PAGE_SIZE)
      paging64_unmap_page(va);
    vma_remove_range(t, addr, addr + size);
  } else {
    if (vma_overlap(t, addr, addr + size))
      return (uint64_t)-1;
  }

  uint64_t map_flags = 0x5; /* P | USER */
  if (prot & USER64_PROT_WRITE)
    map_flags |= 0x2; /* RW */

  for (uint64_t va = addr; va < addr + size; va += USER64_PAGE_SIZE) {
    uint64_t phys = paging64_alloc_frame();
    if (!phys)
      return (uint64_t)-1;
    if (paging64_map_page(va, phys, map_flags) < 0)
      return (uint64_t)-1;
    memset((void *)(uintptr_t)va, 0, USER64_PAGE_SIZE);
  }

  if (addr + size > t->user_mmap_base)
    t->user_mmap_base = addr + size;
  if (vma_add(t, addr, addr + size, prot, flags) < 0)
    return (uint64_t)-1;
  return addr;
}

uint64_t user64_mmap_phys(uint64_t addr, uint64_t len, int prot, int flags,
                          uint64_t phys_base) {
  task64_t *t = task64_current();
  if (!t || len == 0)
    return (uint64_t)-1;

  uint64_t size = align_up(len);
  if (addr == 0) {
    addr = align_up(t->user_mmap_base);
    int guard = 0;
    while (vma_overlap(t, addr, addr + size) && guard++ < 64) {
      addr += size;
    }
  } else {
    if (addr & (USER64_PAGE_SIZE - 1))
      return (uint64_t)-1;
    if (!(flags & USER64_MAP_FIXED))
      addr = align_up(addr);
  }

  if (flags & USER64_MAP_FIXED) {
    for (uint64_t va = addr; va < addr + size; va += USER64_PAGE_SIZE)
      paging64_unmap_page(va);
    vma_remove_range(t, addr, addr + size);
  } else {
    if (vma_overlap(t, addr, addr + size))
      return (uint64_t)-1;
  }

  uint64_t map_flags = 0x5; /* P | USER */
  if (prot & USER64_PROT_WRITE)
    map_flags |= 0x2; /* RW */

  uint64_t phys = align_down(phys_base);
  for (uint64_t va = addr; va < addr + size; va += USER64_PAGE_SIZE) {
    if (paging64_map_page(va, phys, map_flags) < 0)
      return (uint64_t)-1;
    phys += USER64_PAGE_SIZE;
  }

  if (addr + size > t->user_mmap_base)
    t->user_mmap_base = addr + size;
  if (vma_add(t, addr, addr + size, prot, flags) < 0)
    return (uint64_t)-1;
  return addr;
}

int user64_munmap(uint64_t addr, uint64_t len) {
  task64_t *t = task64_current();
  if (!t)
    return -1;
  if (len == 0)
    return -1;
  uint64_t start = align_down(addr);
  uint64_t end = align_up(addr + len);
  for (uint64_t va = start; va < end; va += USER64_PAGE_SIZE)
    paging64_unmap_page(va);
  vma_remove_range(t, start, end);
  return 0;
}

int user64_mprotect(uint64_t addr, uint64_t len, int prot) {
  task64_t *t = task64_current();
  if (!t)
    return -1;
  if (len == 0)
    return -1;
  uint64_t start = align_down(addr);
  uint64_t end = align_up(addr + len);
  uint64_t flags = 0x5; /* P | USER */
  if (prot & USER64_PROT_WRITE)
    flags |= 0x2;
  for (uint64_t va = start; va < end; va += USER64_PAGE_SIZE)
    paging64_protect_page(va, flags);
  vma_update_prot(t, start, end, prot);
  return 0;
}

uint64_t user64_get_sigtramp(void) {
  if (g_sigtramp_ready)
    return USER64_SIGTRAMP_ADDR;

  uint64_t phys = paging64_alloc_frame();
  if (!phys)
    return 0;
  if (paging64_map_page(USER64_SIGTRAMP_ADDR, phys, 0x7) < 0)
    return 0;

  static const uint8_t tramp_code[] = {
      0x48, 0xC7, 0xC0, 0x0F, 0x00, 0x00, 0x00, /* mov rax, 15 */
      0x0F, 0x05,                               /* syscall */
      0x0F, 0x0B                                /* ud2 */
  };

  memcpy((void *)(uintptr_t)USER64_SIGTRAMP_ADDR, tramp_code, sizeof(tramp_code));
  paging64_protect_page(USER64_SIGTRAMP_ADDR, 0x5);
  g_sigtramp_ready = 1;
  return USER64_SIGTRAMP_ADDR;
}
