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

#ifdef __cplusplus
}
#endif
