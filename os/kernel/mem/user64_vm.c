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
#define USER64_MREMAP_MAYMOVE 0x1

#define USER64_VMA_MAX TASK64_VMA_MAX
#define USER64_SIGTRAMP_ADDR 0x000000007fff0000ULL
#define USER64_FB0_DEBUG_MIN 0x00200000ULL

static uint64_t align_up(uint64_t v) {
  return (v + USER64_PAGE_SIZE - 1) & ~(USER64_PAGE_SIZE - 1);
}

static uint64_t align_down(uint64_t v) { return v & ~(USER64_PAGE_SIZE - 1); }

static task64_vma_kind_t flags_to_vma_kind(int flags) {
  if (flags & USER64_VMA_FLAG_DEVICE)
    return TASK64_VMA_DEVICE_PHYS;
  if (flags & USER64_VMA_FLAG_FILE)
    return TASK64_VMA_FILE;
  if (flags & 0x20) /* MAP_ANONYMOUS */
    return TASK64_VMA_ANON;
  return TASK64_VMA_GENERIC;
}

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

static int vma_find_lowest_overlap(task64_t *t, uint64_t start, uint64_t end) {
  int found = -1;
  for (int i = 0; i < USER64_VMA_MAX; i++) {
    if (!t->vmas[i].used)
      continue;
    if (end <= t->vmas[i].start || start >= t->vmas[i].end)
      continue;
    if (found < 0 || t->vmas[i].start < t->vmas[found].start)
      found = i;
  }
  return found;
}

static int vma_find_containing(task64_t *t, uint64_t start, uint64_t end) {
  for (int i = 0; i < USER64_VMA_MAX; i++) {
    if (!t->vmas[i].used)
      continue;
    if (start >= t->vmas[i].start && end <= t->vmas[i].end)
      return i;
  }
  return -1;
}

static int vma_overlap_except(task64_t *t, uint64_t start, uint64_t end,
                              int ignore_idx) {
  for (int i = 0; i < USER64_VMA_MAX; i++) {
    if (i == ignore_idx || !t->vmas[i].used)
      continue;
    if (end <= t->vmas[i].start || start >= t->vmas[i].end)
      continue;
    return 1;
  }
  return 0;
}

static uint64_t prot_to_map_flags(int prot) {
  uint64_t map_flags = 0x5; /* P | USER */
  if (prot & USER64_PROT_WRITE)
    map_flags |= 0x2; /* RW */
  return map_flags;
}

static int g_sigtramp_ready = 0;

static int vma_find_free_slot(task64_t *t) {
  for (int i = 0; i < USER64_VMA_MAX; i++) {
    if (!t->vmas[i].used)
      return i;
  }
  return -1;
}

static uint64_t vma_adjust_backing(const task64_vma_t *vma, uint64_t split_at) {
  if (vma->kind == TASK64_VMA_FILE || vma->kind == TASK64_VMA_DEVICE_PHYS)
    return vma->backing_start + (split_at - vma->start);
  return vma->backing_start;
}

static int vma_can_merge(const task64_vma_t *a, const task64_vma_t *b) {
  if (!a || !b || !a->used || !b->used)
    return 0;
  if (a->end != b->start)
    return 0;
  if (a->prot != b->prot || a->flags != b->flags || a->kind != b->kind)
    return 0;
  if ((a->kind == TASK64_VMA_FILE || a->kind == TASK64_VMA_DEVICE_PHYS) &&
      a->backing_start + (a->end - a->start) != b->backing_start)
    return 0;
  return 1;
}

static int vma_should_trace(task64_t *t, uint64_t start, uint64_t end,
                            task64_vma_kind_t kind) {
  if (!t)
    return 0;
  if (kind == TASK64_VMA_DEVICE_PHYS)
    return 1;
  if (end > start && end - start >= USER64_FB0_DEBUG_MIN)
    return 1;
  for (int i = 0; i < USER64_VMA_MAX; i++) {
    if (!t->vmas[i].used)
      continue;
    if (t->vmas[i].kind != TASK64_VMA_DEVICE_PHYS)
      continue;
    uint64_t pad = USER64_PAGE_SIZE * 4;
    uint64_t lo = t->vmas[i].start > pad ? t->vmas[i].start - pad : 0;
    uint64_t hi = t->vmas[i].end + pad;
    if (!(end <= lo || start >= hi))
      return 1;
  }
  return 0;
}

static void vma_dump(task64_t *t, const char *tag) {
  if (!t)
    return;
  serial_printf("[VMA] dump %s count=%d mmap_base=0x%x%x\r\n", tag ? tag : "-",
                USER64_VMA_MAX,
                (unsigned)(t->user_mmap_base >> 32),
                (unsigned)t->user_mmap_base);
  for (int i = 0; i < USER64_VMA_MAX; i++) {
    if (!t->vmas[i].used)
      continue;
    serial_printf("[VMA]  #%d 0x%x%x-0x%x%x prot=%d flags=0x%x kind=%u back=0x%x%x\r\n",
                  i,
                  (unsigned)(t->vmas[i].start >> 32),
                  (unsigned)t->vmas[i].start,
                  (unsigned)(t->vmas[i].end >> 32),
                  (unsigned)t->vmas[i].end,
                  t->vmas[i].prot,
                  (unsigned)t->vmas[i].flags,
                  (unsigned)t->vmas[i].kind,
                  (unsigned)(t->vmas[i].backing_start >> 32),
                  (unsigned)t->vmas[i].backing_start);
  }
}

static int vma_validate(task64_t *t, const char *tag) {
  if (!t)
    return -1;
  for (int i = 0; i < USER64_VMA_MAX; i++) {
    if (!t->vmas[i].used)
      continue;
    if ((t->vmas[i].start & (USER64_PAGE_SIZE - 1)) ||
        (t->vmas[i].end & (USER64_PAGE_SIZE - 1)) ||
        t->vmas[i].end <= t->vmas[i].start) {
      serial_printf("[VMA] invalid range tag=%s idx=%d\r\n", tag ? tag : "-", i);
      vma_dump(t, tag);
      return -1;
    }
    if (t->vmas[i].kind == TASK64_VMA_DEVICE_PHYS &&
        (t->vmas[i].backing_start & (USER64_PAGE_SIZE - 1))) {
      serial_printf("[VMA] invalid device backing tag=%s idx=%d\r\n",
                    tag ? tag : "-", i);
      vma_dump(t, tag);
      return -1;
    }
    for (int j = i + 1; j < USER64_VMA_MAX; j++) {
      if (!t->vmas[j].used)
        continue;
      if (!(t->vmas[i].end <= t->vmas[j].start ||
            t->vmas[i].start >= t->vmas[j].end)) {
        serial_printf("[VMA] overlap tag=%s a=%d b=%d\r\n", tag ? tag : "-",
                      i, j);
        vma_dump(t, tag);
        return -1;
      }
      if (vma_can_merge(&t->vmas[i], &t->vmas[j]) ||
          vma_can_merge(&t->vmas[j], &t->vmas[i])) {
        serial_printf("[VMA] mergeable neighbors remain tag=%s a=%d b=%d\r\n",
                      tag ? tag : "-", i, j);
        vma_dump(t, tag);
        return -1;
      }
    }
  }
  return 0;
}

static void vma_merge_all(task64_t *t) {
  if (!t)
    return;
  int changed;
  do {
    changed = 0;
    for (int i = 0; i < USER64_VMA_MAX; i++) {
      if (!t->vmas[i].used)
        continue;
      for (int j = 0; j < USER64_VMA_MAX; j++) {
        if (i == j || !t->vmas[j].used)
          continue;
        if (!vma_can_merge(&t->vmas[i], &t->vmas[j]))
          continue;
        t->vmas[i].end = t->vmas[j].end;
        t->vmas[j].used = 0;
        changed = 1;
      }
    }
  } while (changed);
}

static uint64_t vma_find_unmapped_area(task64_t *t, uint64_t hint,
                                       uint64_t size) {
  if (!t || size == 0)
    return 0;
  uint64_t candidate = hint;
  if (candidate < USER64_MMAP_BASE)
    candidate = USER64_MMAP_BASE;
  candidate = align_up(candidate);
  for (int guard = 0; guard < USER64_VMA_MAX * 2; guard++) {
    int overlap = vma_find_lowest_overlap(t, candidate, candidate + size);
    if (overlap < 0)
      return candidate;
    candidate = align_up(t->vmas[overlap].end);
  }
  return 0;
}

static int vma_add_entry(task64_t *t, const task64_vma_t *entry) {
  int idx = vma_find_free_slot(t);
  if (idx >= 0) {
    t->vmas[idx] = *entry;
    t->vmas[idx].used = 1;
    return idx;
  }
  serial_write_string("[VMA] Error: VMA table full!\r\n");
  return -1;
}

static int vma_add(task64_t *t, uint64_t start, uint64_t end, int prot,
                   int flags, task64_vma_kind_t kind, uint64_t backing_start) {
  task64_vma_t entry;
  memset(&entry, 0, sizeof(entry));
  entry.used = 1;
  entry.start = start;
  entry.end = end;
  entry.prot = prot;
  entry.flags = flags;
  entry.kind = (uint8_t)kind;
  entry.backing_start = backing_start;
  return vma_add_entry(t, &entry);
}

static void vma_note_mmap_frontier(task64_t *t, uint64_t start, uint64_t end) {
  if (!t)
    return;
  if (t->user_mmap_base < USER64_MMAP_BASE)
    t->user_mmap_base = USER64_MMAP_BASE;
  if (start <= t->user_mmap_base && end > t->user_mmap_base)
    t->user_mmap_base = end;
}

static int vma_split_at(task64_t *t, int idx, uint64_t split) {
  task64_vma_t *vma = &t->vmas[idx];
  if (!vma->used)
    return -1;
  if (split <= vma->start || split >= vma->end)
    return idx;

  task64_vma_t right = *vma;
  right.start = split;
  right.backing_start = vma_adjust_backing(vma, split);
  vma->end = split;
  if (vma_add_entry(t, &right) < 0)
    return -1;
  return idx;
}

static int vma_range_fully_mapped(task64_t *t, uint64_t start, uint64_t end) {
  uint64_t pos = start;
  while (pos < end) {
    int found = 0;
    uint64_t next = end;
    for (int i = 0; i < USER64_VMA_MAX; i++) {
      if (!t->vmas[i].used)
        continue;
      if (t->vmas[i].start > pos || t->vmas[i].end <= pos)
        continue;
      found = 1;
      if (t->vmas[i].end < next)
        next = t->vmas[i].end;
    }
    if (!found)
      return 0;
    pos = next;
  }
  return 1;
}

static int vma_prepare_range(task64_t *t, uint64_t start, uint64_t end) {
  for (int i = 0; i < USER64_VMA_MAX; i++) {
    if (!t->vmas[i].used)
      continue;
    if (end <= t->vmas[i].start || start >= t->vmas[i].end)
      continue;
    if (vma_split_at(t, i, start) < 0)
      return -1;
    if (vma_split_at(t, i, end) < 0)
      return -1;
  }
  return 0;
}

static void vma_update_prot(task64_t *t, uint64_t start, uint64_t end,
                            int prot) {
  for (int i = 0; i < USER64_VMA_MAX; i++) {
    if (!t->vmas[i].used)
      continue;
    if (start <= t->vmas[i].start && end >= t->vmas[i].end)
      t->vmas[i].prot = prot;
  }
}

static void vma_grow_in_place(task64_vma_t *vma, uint64_t new_end) {
  vma->end = new_end;
}

static void vma_move_range(uint64_t dst, uint64_t src, uint64_t len) {
  memmove((void *)(uintptr_t)dst, (const void *)(uintptr_t)src, (size_t)len);
}

static int vma_map_new_pages(uint64_t start, uint64_t end, int prot) {
  uint64_t map_flags = prot_to_map_flags(prot);
  for (uint64_t va = start; va < end; va += USER64_PAGE_SIZE) {
    uint64_t phys = paging64_alloc_frame();
    if (!phys)
      return -1;
    if (paging64_map_page(va, phys, map_flags) < 0)
      return -1;
    memset((void *)(uintptr_t)va, 0, USER64_PAGE_SIZE);
  }
  return 0;
}

static int vma_unmap_pages(uint64_t start, uint64_t end) {
  for (uint64_t va = start; va < end; va += USER64_PAGE_SIZE) {
    if (paging64_unmap_page(va) < 0)
      return -1;
  }
  return 0;
}

static int vma_protect_pages(uint64_t start, uint64_t end, int prot) {
  uint64_t flags = prot_to_map_flags(prot);
  for (uint64_t va = start; va < end; va += USER64_PAGE_SIZE) {
    if (paging64_protect_page(va, flags) < 0)
      return -1;
  }
  return 0;
}

static int vma_kind_allows_remap(task64_vma_kind_t kind) {
  return kind == TASK64_VMA_ANON || kind == TASK64_VMA_FILE;
}

static int vma_remove_exact_range(task64_t *t, uint64_t start, uint64_t end) {
  if (vma_prepare_range(t, start, end) < 0)
    return -1;
  for (int i = 0; i < USER64_VMA_MAX; i++) {
    if (!t->vmas[i].used)
      continue;
    if (t->vmas[i].start >= start && t->vmas[i].end <= end)
      t->vmas[i].used = 0;
  }
  return 0;
}

static int vma_replace_range(task64_t *t, uint64_t start, uint64_t end) {
  if (!t)
    return -1;
  if (vma_prepare_range(t, start, end) < 0)
    return -1;
  for (int i = 0; i < USER64_VMA_MAX; i++) {
    if (!t->vmas[i].used)
      continue;
    if (t->vmas[i].start < start || t->vmas[i].end > end)
      continue;
    if (vma_unmap_pages(t->vmas[i].start, t->vmas[i].end) < 0)
      return -1;
    t->vmas[i].used = 0;
  }
  vma_merge_all(t);
  return 0;
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
  if (t->user_mmap_base < USER64_MMAP_BASE)
    t->user_mmap_base = USER64_MMAP_BASE;
}

void user64_reset_process(void) {
  task64_t *t = task64_current();
  if (!t)
    return;
  t->user_brk_start = 0;
  t->user_brk_end = 0;
  t->user_mmap_base = USER64_MMAP_BASE;
  for (int i = 0; i < USER64_VMA_MAX; i++)
    t->vmas[i].used = 0;
}

void user64_debug_dump_vmas(const char *tag) {
  task64_t *t = task64_current();
  vma_dump(t, tag);
}

int user64_register_vma(uint64_t start, uint64_t end, int prot, int flags) {
  task64_t *t = task64_current();
  if (!t)
    return -1;
  start = align_down(start);
  end = align_up(end);
  if (end <= start)
    return -1;
  if (vma_overlap(t, start, end))
    return -1;
  if (vma_add(t, start, end, prot, flags, TASK64_VMA_GENERIC, 0) < 0)
    return -1;
  vma_note_mmap_frontier(t, start, end);
  vma_merge_all(t);
  vma_validate(t, "register");
  return 0;
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
  uint64_t hint = addr;
  if (hint && (hint & (USER64_PAGE_SIZE - 1)))
    return (uint64_t)-1;
  if (!(flags & USER64_MAP_FIXED)) {
    uint64_t base_hint = hint ? align_up(hint) : t->user_mmap_base;
    addr = vma_find_unmapped_area(t, base_hint, size);
    if (!addr)
      return (uint64_t)-1;
  } else {
    addr = align_up(addr);
  }

  if (flags & USER64_MAP_FIXED) {
    if (vma_replace_range(t, addr, addr + size) < 0)
      return (uint64_t)-1;
  } else {
    if (vma_overlap(t, addr, addr + size))
      return (uint64_t)-1;
  }

  if (vma_map_new_pages(addr, addr + size, prot) < 0)
    return (uint64_t)-1;

  vma_note_mmap_frontier(t, addr, addr + size);
  if (vma_add(t, addr, addr + size, prot, flags, flags_to_vma_kind(flags), 0) < 0)
    return (uint64_t)-1;
  vma_merge_all(t);
  if (vma_validate(t, "mmap") < 0)
    return (uint64_t)-1;
  if (vma_should_trace(t, addr, addr + size, flags_to_vma_kind(flags))) {
    serial_printf("[VMA] mmap result addr=0x%x%x len=0x%x%x kind=%u flags=0x%x\r\n",
                  (unsigned)(addr >> 32), (unsigned)addr,
                  (unsigned)(size >> 32), (unsigned)size,
                  (unsigned)flags_to_vma_kind(flags), (unsigned)flags);
    vma_dump(t, "mmap");
  }
  return addr;
}

uint64_t user64_mmap_phys(uint64_t addr, uint64_t len, int prot, int flags,
                          uint64_t phys_base) {
  task64_t *t = task64_current();
  if (!t || len == 0)
    return (uint64_t)-1;

  uint64_t size = align_up(len);
  uint64_t hint = addr;
  if (hint && (hint & (USER64_PAGE_SIZE - 1)))
    return (uint64_t)-1;
  if (!(flags & USER64_MAP_FIXED)) {
    uint64_t base_hint = hint ? align_up(hint) : t->user_mmap_base;
    addr = vma_find_unmapped_area(t, base_hint, size);
    if (!addr)
      return (uint64_t)-1;
  } else {
    addr = align_up(addr);
  }

  if (flags & USER64_MAP_FIXED) {
    if (vma_replace_range(t, addr, addr + size) < 0)
      return (uint64_t)-1;
  } else {
    if (vma_overlap(t, addr, addr + size))
      return (uint64_t)-1;
  }

  uint64_t map_flags = prot_to_map_flags(prot);

  uint64_t phys = align_down(phys_base);
  for (uint64_t va = addr; va < addr + size; va += USER64_PAGE_SIZE) {
    if (paging64_map_page(va, phys, map_flags) < 0)
      return (uint64_t)-1;
    phys += USER64_PAGE_SIZE;
  }

  vma_note_mmap_frontier(t, addr, addr + size);
  if (vma_add(t, addr, addr + size, prot, flags | USER64_VMA_FLAG_DEVICE,
              TASK64_VMA_DEVICE_PHYS, align_down(phys_base)) < 0)
    return (uint64_t)-1;
  vma_merge_all(t);
  if (vma_validate(t, "mmap_phys") < 0)
    return (uint64_t)-1;
  serial_printf("[VMA] mmap_phys result addr=0x%x%x len=0x%x%x phys=0x%x%x\r\n",
                (unsigned)(addr >> 32), (unsigned)addr,
                (unsigned)(size >> 32), (unsigned)size,
                (unsigned)(align_down(phys_base) >> 32),
                (unsigned)align_down(phys_base));
  vma_dump(t, "mmap_phys");
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
  if (vma_prepare_range(t, start, end) < 0)
    return -1;
  for (int i = 0; i < USER64_VMA_MAX; i++) {
    if (!t->vmas[i].used)
      continue;
    if (t->vmas[i].start < start || t->vmas[i].end > end)
      continue;
    if (vma_unmap_pages(t->vmas[i].start, t->vmas[i].end) < 0)
      return -1;
    t->vmas[i].used = 0;
  }
  vma_merge_all(t);
  if (vma_validate(t, "munmap") < 0)
    return -1;
  return 0;
}

uint64_t user64_mremap(uint64_t old_addr, uint64_t old_len, uint64_t new_len,
                       uint64_t flags) {
  task64_t *t = task64_current();
  if (!t || old_len == 0 || new_len == 0)
    return (uint64_t)-1;
  if (flags & ~USER64_MREMAP_MAYMOVE)
    return (uint64_t)-1;

  uint64_t old_start = align_down(old_addr);
  uint64_t old_size = align_up(old_len);
  uint64_t new_size = align_up(new_len);
  uint64_t old_end = old_start + old_size;
  uint64_t new_end = old_start + new_size;

  vma_merge_all(t);

  int idx = vma_find_containing(t, old_start, old_end);
  if (idx < 0)
    return (uint64_t)-1;

  int prot = t->vmas[idx].prot;
  int vma_flags = t->vmas[idx].flags;
  task64_vma_kind_t kind = (task64_vma_kind_t)t->vmas[idx].kind;
  uint64_t backing_start = t->vmas[idx].backing_start;

  if (!vma_kind_allows_remap(kind))
    return (uint64_t)-1;

  if (new_size == old_size)
    return old_start;

  if (new_size < old_size) {
    if (user64_munmap(old_start + new_size, old_size - new_size) < 0)
      return (uint64_t)-1;
    return old_start;
  }

  if (t->vmas[idx].end == old_end &&
      !vma_overlap_except(t, old_end, new_end, idx)) {
    if (vma_map_new_pages(old_end, new_end, prot) < 0)
      return (uint64_t)-1;
    vma_grow_in_place(&t->vmas[idx], new_end);
    vma_note_mmap_frontier(t, old_start, new_end);
    vma_merge_all(t);
    if (vma_validate(t, "mremap-grow") < 0)
      return (uint64_t)-1;
    return old_start;
  }

  if (!(flags & USER64_MREMAP_MAYMOVE))
    return (uint64_t)-1;

  uint64_t move_flags = (uint64_t)(vma_flags & ~USER64_MAP_FIXED);
  uint64_t new_addr = user64_mmap(0, new_size, prot, (int)move_flags);
  if ((int64_t)new_addr < 0)
    return (uint64_t)-1;

  vma_move_range(new_addr, old_start, old_size < new_size ? old_size : new_size);
  if (user64_munmap(old_start, old_size) < 0) {
    user64_munmap(new_addr, new_size);
    return (uint64_t)-1;
  }
  int new_idx = vma_find_containing(t, new_addr, new_addr + new_size);
  if (new_idx >= 0) {
    t->vmas[new_idx].kind = (uint8_t)kind;
    t->vmas[new_idx].backing_start = backing_start;
  }
  vma_merge_all(t);
  if (vma_validate(t, "mremap-move") < 0)
    return (uint64_t)-1;
  return new_addr;
}

int user64_mprotect(uint64_t addr, uint64_t len, int prot) {
  task64_t *t = task64_current();
  if (!t)
    return -1;
  if (len == 0)
    return -1;
  uint64_t start = align_down(addr);
  uint64_t end = align_up(addr + len);
  if (!vma_range_fully_mapped(t, start, end))
    return -1;
  if (vma_prepare_range(t, start, end) < 0)
    return -1;
  if (vma_protect_pages(start, end, prot) < 0)
    return -1;
  vma_update_prot(t, start, end, prot);
  vma_merge_all(t);
  if (vma_validate(t, "mprotect") < 0)
    return -1;
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
