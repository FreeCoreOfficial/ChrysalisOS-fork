#include "paging64.h"
#include <stddef.h>

extern "C" uint64_t pml4;
extern "C" uint64_t pdpt;
extern "C" uint64_t pd;

extern "C" char kernel64_end;
extern "C" uint64_t pmm64_alloc_frame(void);
extern "C" int pmm64_is_ready(void);

static uint64_t *g_pml4 = 0;
static uint64_t g_next_phys = 0;
static uint64_t g_max_phys = 0;

static void *k_memset(void *s, int c, size_t n) {
  unsigned char *p = (unsigned char *)s;
  while (n--)
    *p++ = (unsigned char)c;
  return s;
}

static uint64_t align_up(uint64_t v, uint64_t a) {
  return (v + (a - 1)) & ~(a - 1);
}

void paging64_init(void) {
  g_pml4 = &pml4;

  uint64_t start = (uint64_t)(unsigned long long)&kernel64_end;
  g_next_phys = align_up(start, 0x1000);
  g_max_phys = 0x100000000ULL; /* 4 GiB */
}

uint64_t paging64_alloc_frame(void) {
  if (pmm64_is_ready()) {
    uint64_t phys = pmm64_alloc_frame();
    if (phys)
      return phys;
  }
  if (g_next_phys == 0)
    paging64_init();
  if (g_next_phys + 0x1000 > g_max_phys)
    return 0;
  uint64_t phys = g_next_phys;
  g_next_phys += 0x1000;
  return phys;
}

void *paging64_alloc_page(void) {
  uint64_t phys = paging64_alloc_frame();
  if (!phys)
    return 0;
  void *virt = (void *)(uint64_t)phys; /* identity mapped */
  k_memset(virt, 0, 0x1000);
  return virt;
}

static int ensure_table(uint64_t *entry, int user) {
  if (*entry & 0x1) {
    if (user)
      *entry |= 0x4;
    return 0;
  }
  uint64_t phys = paging64_alloc_frame();
  if (!phys)
    return -1;
  void *virt = (void *)(uint64_t)phys;
  k_memset(virt, 0, 0x1000);
  uint64_t flags = 0x3 | (user ? 0x4 : 0); /* P | RW | (USER) */
  *entry = phys | flags;
  return 0;
}

static int split_huge_pd(uint64_t *pd_entry) {
  if (!pd_entry || !(*pd_entry & 0x80))
    return 0;
  uint64_t base = *pd_entry & ~0x1FFFFFULL;
  uint64_t phys = paging64_alloc_frame();
  if (!phys)
    return -1;
  uint64_t *pt = (uint64_t *)(uint64_t)phys;
  uint64_t flags = (*pd_entry & 0xFFFULL) & ~0x80ULL;
  for (uint64_t i = 0; i < 512; ++i) {
    pt[i] = (base + (i * 0x1000ULL)) | flags | 0x1;
  }
  *pd_entry = phys | flags | 0x1;
  return 0;
}

int paging64_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
  if (!g_pml4)
    paging64_init();

  uint64_t pml4_i = (virt >> 39) & 0x1FF;
  uint64_t pdpt_i = (virt >> 30) & 0x1FF;
  uint64_t pd_i = (virt >> 21) & 0x1FF;
  uint64_t pt_i = (virt >> 12) & 0x1FF;

  uint64_t *pml4_tab = g_pml4;
  if (ensure_table(&pml4_tab[pml4_i], (flags & 0x4) != 0) < 0)
    return -1;
  uint64_t *pdpt_tab = (uint64_t *)(uint64_t)(pml4_tab[pml4_i] & ~0xFFFULL);

  if (ensure_table(&pdpt_tab[pdpt_i], (flags & 0x4) != 0) < 0)
    return -1;
  uint64_t *pd_tab = (uint64_t *)(uint64_t)(pdpt_tab[pdpt_i] & ~0xFFFULL);

  if (pd_tab[pd_i] & 0x80) {
    if (split_huge_pd(&pd_tab[pd_i]) < 0)
      return -1;
  }

  if (ensure_table(&pd_tab[pd_i], (flags & 0x4) != 0) < 0)
    return -1;
  uint64_t *pt_tab = (uint64_t *)(uint64_t)(pd_tab[pd_i] & ~0xFFFULL);

  pt_tab[pt_i] = (phys & ~0xFFFULL) | (flags & 0xFFFULL) | 0x1;
  return 0;
}

uint64_t *paging64_get_pml4(void) { return g_pml4; }
