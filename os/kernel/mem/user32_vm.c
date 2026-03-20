#include "user32_vm.h"
#include "../mm/vmm.h"
#include "../mm/paging.h"
#include "../include/task.h"
#include "../string.h"

#define USER32_PAGE_SIZE 0x1000u
#define USER32_HEAP_MIN  0x10000000u
#define USER32_MMAP_BASE 0x60000000u

#define USER32_VMA_MAX 64

#define USER32_PROT_WRITE 0x2
#define USER32_MAP_FIXED  0x10

static uint32_t align_up(uint32_t v) {
  return (v + USER32_PAGE_SIZE - 1u) & ~(USER32_PAGE_SIZE - 1u);
}

static uint32_t align_down(uint32_t v) { return v & ~(USER32_PAGE_SIZE - 1u); }

static user32_vma_t *user32_vmas(task_t *t) {
  return (user32_vma_t *)t->user_vmas;
}

static uint32_t *user32_pd(task_t *t) {
  if (!t)
    return NULL;
  if (t->cr3)
    return (uint32_t *)(uintptr_t)(t->cr3 + KERNEL_BASE);
  extern uint32_t *kernel_page_directory;
  return kernel_page_directory;
}

static int vma_overlap(task_t *t, uint32_t start, uint32_t end) {
  user32_vma_t *vmas = user32_vmas(t);
  for (int i = 0; i < USER32_VMA_MAX; i++) {
    if (!vmas[i].used)
      continue;
    if (end <= vmas[i].start || start >= vmas[i].end)
      continue;
    return 1;
  }
  return 0;
}

static int vma_add(task_t *t, uint32_t start, uint32_t end, int prot,
                   int flags) {
  user32_vma_t *vmas = user32_vmas(t);
  for (int i = 0; i < USER32_VMA_MAX; i++) {
    if (!vmas[i].used) {
      vmas[i].used = 1;
      vmas[i].start = start;
      vmas[i].end = end;
      vmas[i].prot = prot;
      vmas[i].flags = flags;
      return 0;
    }
  }
  return -1;
}

static void vma_remove_range(task_t *t, uint32_t start, uint32_t end) {
  user32_vma_t *vmas = user32_vmas(t);
  for (int i = 0; i < USER32_VMA_MAX; i++) {
    if (!vmas[i].used)
      continue;
    uint32_t vs = vmas[i].start;
    uint32_t ve = vmas[i].end;
    if (end <= vs || start >= ve)
      continue;
    if (start <= vs && end >= ve) {
      vmas[i].used = 0;
      continue;
    }
    if (start > vs && end < ve) {
      uint32_t old_end = ve;
      vmas[i].end = start;
      vma_add(t, end, old_end, vmas[i].prot, vmas[i].flags);
      continue;
    }
    if (start <= vs && end < ve) {
      vmas[i].start = end;
      continue;
    }
    if (start > vs && end >= ve) {
      vmas[i].end = start;
      continue;
    }
  }
}

static void vma_update_prot(task_t *t, uint32_t start, uint32_t end, int prot) {
  user32_vma_t *vmas = user32_vmas(t);
  for (int i = 0; i < USER32_VMA_MAX; i++) {
    if (!vmas[i].used)
      continue;
    uint32_t vs = vmas[i].start;
    uint32_t ve = vmas[i].end;
    if (end <= vs || start >= ve)
      continue;
    if (start <= vs && end >= ve) {
      vmas[i].prot = prot;
      continue;
    }
    if (start > vs && end < ve) {
      uint32_t old_end = ve;
      int old_prot = vmas[i].prot;
      int old_flags = vmas[i].flags;
      vmas[i].end = start;
      vma_add(t, start, end, prot, old_flags);
      vma_add(t, end, old_end, old_prot, old_flags);
      continue;
    }
    if (start <= vs && end < ve) {
      vmas[i].start = end;
      vma_add(t, vs, end, prot, vmas[i].flags);
      continue;
    }
    if (start > vs && end >= ve) {
      vmas[i].end = start;
      vma_add(t, start, ve, prot, vmas[i].flags);
      continue;
    }
  }
}

void user32_init_process(task_t *t, uint32_t image_end) {
  if (!t)
    return;
  uint32_t base = align_up(image_end);
  if (base < USER32_HEAP_MIN)
    base = USER32_HEAP_MIN;
  t->user_brk_start = base;
  t->user_brk_end = base;
  t->user_mmap_base = USER32_MMAP_BASE;
  memset(t->user_vmas, 0, sizeof(t->user_vmas));
}

uint32_t user32_brk(task_t *t, uint32_t new_brk) {
  if (!t)
    return 0;
  if (new_brk == 0)
    return t->user_brk_end;
  if (new_brk < t->user_brk_start)
    return t->user_brk_end;

  uint32_t old = t->user_brk_end;
  uint32_t start = align_up(old);
  uint32_t end = align_up(new_brk);

  uint32_t *pd = user32_pd(t);
  if (!pd)
    return old;

  for (uint32_t va = start; va < end; va += USER32_PAGE_SIZE) {
    void *page = vmm_alloc_page();
    if (!page)
      return old;
    uint32_t phys = vmm_virt_to_phys(page);
    if (!phys)
      return old;
    vmm_map_page(pd, va, phys, PAGE_PRESENT | PAGE_RW | PAGE_USER);
    memset(page, 0, USER32_PAGE_SIZE);
  }

  t->user_brk_end = new_brk;
  return t->user_brk_end;
}

uint32_t user32_mmap(task_t *t, uint32_t addr, uint32_t len, int prot,
                     int flags) {
  if (!t || len == 0)
    return (uint32_t)-1;
  uint32_t size = align_up(len);

  if (addr == 0) {
    addr = align_up(t->user_mmap_base);
    int guard = 0;
    while (vma_overlap(t, addr, addr + size) && guard++ < 64) {
      addr += size;
    }
  } else {
    if (addr & (USER32_PAGE_SIZE - 1u))
      return (uint32_t)-1;
    if (!(flags & USER32_MAP_FIXED))
      addr = align_up(addr);
  }

  uint32_t *pd = user32_pd(t);
  if (!pd)
    return (uint32_t)-1;

  if (flags & USER32_MAP_FIXED) {
    for (uint32_t va = addr; va < addr + size; va += USER32_PAGE_SIZE)
      vmm_unmap_page(pd, va);
    vma_remove_range(t, addr, addr + size);
  } else {
    if (vma_overlap(t, addr, addr + size))
      return (uint32_t)-1;
  }

  uint32_t map_flags = PAGE_PRESENT | PAGE_USER;
  if (prot & USER32_PROT_WRITE)
    map_flags |= PAGE_RW;

  for (uint32_t va = addr; va < addr + size; va += USER32_PAGE_SIZE) {
    void *page = vmm_alloc_page();
    if (!page)
      return (uint32_t)-1;
    uint32_t phys = vmm_virt_to_phys(page);
    if (!phys)
      return (uint32_t)-1;
    vmm_map_page(pd, va, phys, map_flags);
    memset(page, 0, USER32_PAGE_SIZE);
  }

  if (addr + size > t->user_mmap_base)
    t->user_mmap_base = addr + size;
  if (vma_add(t, addr, addr + size, prot, flags) < 0)
    return (uint32_t)-1;
  return addr;
}

int user32_munmap(task_t *t, uint32_t addr, uint32_t len) {
  if (!t || len == 0)
    return -1;
  uint32_t start = align_down(addr);
  uint32_t end = align_up(addr + len);
  uint32_t *pd = user32_pd(t);
  if (!pd)
    return -1;
  for (uint32_t va = start; va < end; va += USER32_PAGE_SIZE)
    vmm_unmap_page(pd, va);
  vma_remove_range(t, start, end);
  return 0;
}

int user32_mprotect(task_t *t, uint32_t addr, uint32_t len, int prot) {
  if (!t || len == 0)
    return -1;
  uint32_t start = align_down(addr);
  uint32_t end = align_up(addr + len);
  uint32_t *pd = user32_pd(t);
  if (!pd)
    return -1;
  uint32_t flags = PAGE_PRESENT | PAGE_USER;
  if (prot & USER32_PROT_WRITE)
    flags |= PAGE_RW;

  for (uint32_t va = start; va < end; va += USER32_PAGE_SIZE) {
    uint32_t *pte = get_pte_for(pd, va, 0);
    if (!pte || !(*pte & PAGE_PRESENT))
      continue;
    uint32_t phys = *pte & PAGE_FRAME_MASK;
    *pte = phys | flags;
    asm volatile("invlpg (%0)" : : "r"(va) : "memory");
  }

  vma_update_prot(t, start, end, prot);
  return 0;
}
