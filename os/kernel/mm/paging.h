#pragma once

/* kernel/mm/paging.h
 * Paging helper API for Chrysalis OS
 * - Defines page flags
 * - Prototypes for kernel init + user pagetable helpers
 *
 * Notes:
 *  - Kernel mappings must NOT include PAGE_USER.
 *  - User mappings should include PAGE_USER.
 */

#include <stdint.h>

#ifndef MM_PAGING_H
#define MM_PAGING_H

#ifdef __cplusplus
extern "C" {
#endif

/* Page flags */
#ifndef PAGE_PRESENT
#define PAGE_PRESENT 0x01
#endif
#ifndef PAGE_RW
#define PAGE_RW      0x02
#endif
#ifndef PAGE_USER
#define PAGE_USER    0x04
#endif
#define PAGE_PWT     0x08 /* Page Write Through */
#define PAGE_PCD     0x10 /* Page Cache Disable */

/* Convenience masks */
#define PAGE_FRAME_MASK 0xFFFFF000u

/* Kernel base virtual (3GB) used by the paging layout */
#define KERNEL_BASE 0xC0000000u

/* Initializes kernel page tables and enables paging.
 * Must be called early from the boot/init path.
 */
void paging_init_kernel(void);

/* Load a page directory by physical address into CR3
 * (helper used by address_space_switch)
 */
void paging_load_directory(uint32_t pd_phys);

/* Allocate and return a fresh user page directory.
 * Returns a physical pointer (uint32_t) cast to void* that
 * references the page-directory page.
 * The returned directory will contain the kernel high mappings
 * copied from the kernel page directory (so kernel stays mapped)
 * and empty user-space entries.
 */
void* create_user_pagetable(void);

/* Map a single user page into the provided page-directory.
 * - pagedir : physical pointer to the page-directory page (as returned by create_user_pagetable)
 * - vaddr   : virtual address to map (must be page-aligned)
 * - phys    : physical frame to map (must be page-aligned)
 * - flags   : combination of PAGE_PRESENT | PAGE_RW | PAGE_USER ...
 *
 * This helper will create page tables if needed and set the
 * PTE/PDE entries. It expects 4KiB pages and 32-bit paging.
 */
void map_user_page(void* pagedir, void* vaddr, void* phys, uint32_t flags);

/* Map a kernel page into the kernel page directory. This is a small
 * wrapper used during early kernel mappings. IMPORTANT: do not pass
 * PAGE_USER here (kernel must be U/S == 0).
 */
void map_kernel_page(uint32_t vaddr, uint32_t phys, uint32_t flags);

/* Low-level helpers exposed for the rest of the kernel (optional)
 * - get_pte/get_pde return pointers to the entries (virtual addresses
 *   inside the kernel's address space). If 'create' is non-zero the
 *   function will allocate a new page table (using your kernel allocator)
 *   when a second-level page table is missing.
 */
uint32_t* get_pde_for(uint32_t* pagedir, uint32_t vaddr, int create);
uint32_t* get_pte_for(uint32_t* pagedir, uint32_t vaddr, int create);

/* Kernel page directory (kernel-virtual pointer) */
extern uint32_t* kernel_page_directory;

#ifdef __cplusplus
}
#endif

#endif /* MM_PAGING_H */
