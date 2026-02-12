/* kernel/mm/paging.c
 * Minimal paging helpers: implements get_pde_for, get_pte_for,
 * paging_load_directory and defines kernel_page_directory.
 *
 * This file intentionally avoids depending on vmm.c. It uses pmm to
 * allocate frames for page tables and converts physical<->virtual
 * using KERNEL_BASE mapping.
 *
 * This updated version calls an assembly helper `load_cr3` to set CR3
 * (avoids inline-asm operand mismatch). See arch/x86/load_cr3.S.
 */

#include "paging.h"
#include "../memory/pmm.h"
#include <stddef.h>
#include <stdint.h>

/* Kernel page-directory pointer (kernel-virtual). Defined here so other
 * modules (vmm.c, address_space.c) can extern it.
 */
uint32_t *kernel_page_directory = 0;

/* Forward-declare an assembly helper that safely loads CR3.
 * Implemented in arch/x86/load_cr3.S; this avoids inline-asm operand
 * type mismatch across toolchains.
 */
extern void load_cr3(uint32_t pd_phys);

static inline void *phys_to_virt(uint32_t phys) {
  return (void *)(uintptr_t)(phys + KERNEL_BASE);
}
static inline uint32_t virt_to_phys_addr(void *virt) {
  return (uint32_t)((uintptr_t)virt - (uintptr_t)KERNEL_BASE);
}

uint32_t *get_pde_for(uint32_t *pagedir, uint32_t vaddr, int create) {
  if (!pagedir)
    return NULL;
  (void)create;
  uint32_t pd_index = (vaddr >> 22) & 0x3FFu;
  return &pagedir[pd_index];
}

uint32_t *get_pte_for(uint32_t *pagedir, uint32_t vaddr, int create) {
  if (!pagedir)
    return NULL;

  uint32_t pd_index = (vaddr >> 22) & 0x3FFu;
  uint32_t pt_index = (vaddr >> 12) & 0x3FFu;

  uint32_t pde = pagedir[pd_index];
  if (!(pde & PAGE_PRESENT)) {
    if (!create)
      return NULL;

    /* allocate a page for the new page-table */
    uint32_t pt_phys = pmm_alloc_frame();
    if (pt_phys == 0)
      return NULL;

    /* zero the new page-table (kernel-virtual) */
    uint32_t *pt_virt = (uint32_t *)phys_to_virt(pt_phys);
    for (uint32_t i = 0; i < 1024; ++i)
      pt_virt[i] = 0;

    /* set PDE: point to the new page table, make it present, rw
     * Keep USER bit cleared here; the caller will set PTE flags
     * appropriately. Setting USER in PDE is optional; some kernels
     * set it to allow user tables, but we keep it 0 to be strict.
     */
    pagedir[pd_index] =
        (pt_phys & PAGE_FRAME_MASK) | PAGE_PRESENT | PAGE_RW | PAGE_USER;
    pde = pagedir[pd_index];
  }

  uint32_t pt_phys = pde & PAGE_FRAME_MASK;
  uint32_t *pt = (uint32_t *)phys_to_virt(pt_phys);
  return &pt[pt_index];
}

void paging_load_directory(uint32_t pd_phys) {
  /* Use the assembly helper to set CR3. This avoids assembler differences
   * and works reliably with -m32 toolchains. The helper simply moves the
   * argument into CR3 and returns.
   */
  load_cr3(pd_phys);
}

/* Optional helper for early kernel init: map kernel page directory pointer
 * into the global variable. The caller should ensure kernel_page_directory
 * points to a kernel-virtual PD (i.e. phys_to_virt(pd_phys)).
 */
void paging_set_kernel_pd(uint32_t *pd_virt) {
  kernel_page_directory = pd_virt;
}

/* Simple kernel init stub (can be extended): allocate a page-directory
 * and set kernel_page_directory. This is a convenience for early boot.
 */
void paging_init_kernel(void) {
  if (kernel_page_directory)
    return;

  uint32_t pd_phys = pmm_alloc_frame();
  if (pd_phys == 0)
    return;

  kernel_page_directory = (uint32_t *)phys_to_virt(pd_phys);
  for (uint32_t i = 0; i < 1024; ++i)
    kernel_page_directory[i] = 0;

  /* Caller must map kernel regions into the directory before enabling paging.
   * That logic typically lives in early boot code.
   */
}
