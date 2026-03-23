#include "paging64.h"
#include <stddef.h>

extern "C" uint64_t pml4;
extern "C" uint64_t pdpt;
extern "C" uint64_t pd;

extern "C" char kernel64_end;
extern "C" uint64_t pmm64_alloc_frame(void);
extern "C" int pmm64_is_ready(void);

static uint64_t *g_pml4 = 0;
static uint64_t *g_boot_pml4 = 0;
static uint64_t g_next_phys = 0;
static uint64_t g_max_phys = 0;

static void *k_memset(void *s, int c, size_t n) {
  unsigned char *p = (unsigned char *)s;
  while (n--)
    *p++ = (unsigned char)c;
  return s;
}

static void *k_memcpy(void *dest, const void *src, size_t n) {
  char *d = (char *)dest;
  const char *s = (const char *)src;
  while (n--) *d++ = *s++;
  return dest;
}

static uint64_t align_up(uint64_t v, uint64_t a) {
  return (v + (a - 1)) & ~(a - 1);
}

void paging64_init(void) {
  g_pml4 = &pml4;
  if (!g_boot_pml4) g_boot_pml4 = g_pml4;

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

static int get_pte(uint64_t virt, uint64_t **out_pte) {
  if (!out_pte)
    return -1;
  if (!g_pml4)
    paging64_init();

  uint64_t pml4_i = (virt >> 39) & 0x1FF;
  uint64_t pdpt_i = (virt >> 30) & 0x1FF;
  uint64_t pd_i = (virt >> 21) & 0x1FF;
  uint64_t pt_i = (virt >> 12) & 0x1FF;

  uint64_t *pml4_tab = g_pml4;
  if (!(pml4_tab[pml4_i] & 0x1))
    return -1;
  uint64_t *pdpt_tab = (uint64_t *)(uint64_t)(pml4_tab[pml4_i] & ~0xFFFULL);
  if (!(pdpt_tab[pdpt_i] & 0x1))
    return -1;
  uint64_t *pd_tab = (uint64_t *)(uint64_t)(pdpt_tab[pdpt_i] & ~0xFFFULL);
  if (!(pd_tab[pd_i] & 0x1))
    return -1;
  if (pd_tab[pd_i] & 0x80) {
    if (split_huge_pd(&pd_tab[pd_i]) < 0)
      return -1;
  }
  if (!(pd_tab[pd_i] & 0x1))
    return -1;
  uint64_t *pt_tab = (uint64_t *)(uint64_t)(pd_tab[pd_i] & ~0xFFFULL);
  *out_pte = &pt_tab[pt_i];
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

int paging64_unmap_page(uint64_t virt) {
  uint64_t *pte = nullptr;
  if (get_pte(virt, &pte) < 0 || !pte)
    return -1;
  if (!(*pte & 0x1))
    return -1;
  *pte = 0;
  asm volatile("invlpg (%0)" ::"r"(virt) : "memory");
  return 0;
}

int paging64_protect_page(uint64_t virt, uint64_t flags) {
  uint64_t *pte = nullptr;
  if (get_pte(virt, &pte) < 0 || !pte)
    return -1;
  if (!(*pte & 0x1))
    return -1;
  uint64_t phys = *pte & ~0xFFFULL;
  *pte = phys | (flags & 0xFFFULL) | 0x1;
  asm volatile("invlpg (%0)" ::"r"(virt) : "memory");
  return 0;
}

extern "C" uint64_t task64_get_cr3(void);

uint64_t *paging64_get_pml4(void) {
  uint64_t task_cr3 = task64_get_cr3();
  if (task_cr3) return (uint64_t *)task_cr3;
  return g_pml4;
}

static uint64_t paging64_deep_copy_pml4(uint64_t *old_pml4, int clone_user_data) {
  uint64_t *new_pml4 = (uint64_t *)paging64_alloc_page();
  if (!new_pml4) return 0;
  
  if (!old_pml4) return (uint64_t)(uintptr_t)new_pml4;

  for (int i = 0; i < 512; i++) {
    if (i >= 256) {
      // Kernel space (upper half): shallow copy
      new_pml4[i] = old_pml4[i];
    } else {
      // User space & Identity map (lower half): deep copy directory structures
      if (!(old_pml4[i] & 0x1)) continue;
      
      uint64_t *new_pdpt = (uint64_t *)paging64_alloc_page();
      if (!new_pdpt) continue;
      new_pml4[i] = ((uint64_t)(uintptr_t)new_pdpt) | (old_pml4[i] & 0xFFF);
      
      uint64_t *old_pdpt = (uint64_t *)(uint64_t)(old_pml4[i] & ~0xFFFULL);
      for (int j = 0; j < 512; j++) {
        if (!(old_pdpt[j] & 0x1)) continue;
        
        uint64_t *new_pd = (uint64_t *)paging64_alloc_page();
        if (!new_pd) continue;
        new_pdpt[j] = ((uint64_t)(uintptr_t)new_pd) | (old_pdpt[j] & 0xFFF);
        
        uint64_t *old_pd = (uint64_t *)(uint64_t)(old_pdpt[j] & ~0xFFFULL);
        for (int k = 0; k < 512; k++) {
          if (!(old_pd[k] & 0x1)) continue;
          
          if (old_pd[k] & 0x80) { // Huge page
            new_pd[k] = old_pd[k]; // Shallow copy huge pages
            continue;
          }
          
          uint64_t *new_pt = (uint64_t *)paging64_alloc_page();
          if (!new_pt) continue;
          new_pd[k] = ((uint64_t)(uintptr_t)new_pt) | (old_pd[k] & 0xFFF);
          
          uint64_t *old_pt = (uint64_t *)(uint64_t)(old_pd[k] & ~0xFFFULL);
          for (int m = 0; m < 512; m++) {
            if (!(old_pt[m] & 0x1)) continue;
            
            if (clone_user_data && (old_pt[m] & 0x4)) {
              // USER page: deep copy data
              void *new_page = paging64_alloc_page();
              if (!new_page) {
                new_pt[m] = old_pt[m]; // Fallback
                continue;
              }
              void *old_page = (void *)(uint64_t)(old_pt[m] & ~0xFFFULL);
              k_memcpy(new_page, old_page, 0x1000);
              new_pt[m] = ((uint64_t)(uintptr_t)new_page) | (old_pt[m] & 0xFFF);
            } else {
              // KERNEL page or skipping user data (execve): shallow copy the entry
              // (but the table itself is private to this process)
              new_pt[m] = old_pt[m];
            }
          }
        }
      }
    }
  }

  return (uint64_t)(uintptr_t)new_pml4;
}

// Deep copy of user space, shallow copy of kernel space
uint64_t paging64_clone_pml4(void) {
  uint64_t *old_pml4 = paging64_get_pml4();
  if (!old_pml4) {
    paging64_init();
    old_pml4 = g_pml4;
  }
  return paging64_deep_copy_pml4(old_pml4, 1);
}

uint64_t paging64_new_user_pml4(void) {
  if (!g_boot_pml4) {
    if (!g_pml4) paging64_init();
    g_boot_pml4 = g_pml4;
  }
  
  /* Create a fresh PML4 by deep-copying directory structures from the boot PML4,
   * but DON'T copy any user data pages (though boot PML4 should have none).
   * This ensures the new process has private tables for its low-half mappings. */
  return paging64_deep_copy_pml4(g_boot_pml4, 0);
}

void paging64_set_pml4(uint64_t cr3) {
  if (cr3 == 0) return;
  /* Only switch hardware CR3 — do NOT update g_pml4.
   * g_pml4 must remain the boot PML4 so kernel page allocation
   * and other operations work on identity-mapped memory. */
  asm volatile("mov %0, %%cr3" ::"r"(cr3) : "memory");
}

void paging64_set_active_pml4(uint64_t cr3) {
  if (cr3 == 0) return;
  /* Save boot PML4 if not yet saved */
  if (!g_boot_pml4) g_boot_pml4 = g_pml4;
  /* Switch both the software pointer AND hardware CR3.
   * Use this only when you need paging64_map_page to target a specific PML4
   * (e.g. mapping user segments into a new address space). */
  g_pml4 = (uint64_t *)(uintptr_t)cr3;
  asm volatile("mov %0, %%cr3" ::"r"(cr3) : "memory");
}

void paging64_restore_boot_pml4(void) {
  if (g_boot_pml4) {
    g_pml4 = g_boot_pml4;
    asm volatile("mov %0, %%cr3" ::"r"((uint64_t)(uintptr_t)g_boot_pml4) : "memory");
  }
}
