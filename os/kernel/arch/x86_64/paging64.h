#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void paging64_init(void);
uint64_t paging64_alloc_frame(void);
void *paging64_alloc_page(void);
int paging64_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
int paging64_unmap_page(uint64_t virt);
int paging64_protect_page(uint64_t virt, uint64_t flags);
uint64_t *paging64_get_pml4(void);
uint64_t paging64_clone_pml4(void);
void paging64_set_pml4(uint64_t cr3);
uint64_t paging64_new_user_pml4(void);
void paging64_set_active_pml4(uint64_t cr3);
void paging64_restore_boot_pml4(void);
void paging64_dump_pte(uint64_t virt);

#ifdef __cplusplus
}
#endif
