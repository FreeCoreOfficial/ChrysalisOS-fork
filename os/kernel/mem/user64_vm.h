#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void user64_init_process(uint64_t image_end);
uint64_t user64_brk(uint64_t new_brk);
uint64_t user64_mmap(uint64_t addr, uint64_t len, int prot, int flags);
uint64_t user64_mmap_phys(uint64_t addr, uint64_t len, int prot, int flags,
                          uint64_t phys_base);
int user64_munmap(uint64_t addr, uint64_t len);
int user64_mprotect(uint64_t addr, uint64_t len, int prot);
uint64_t user64_get_sigtramp(void);

#ifdef __cplusplus
}
#endif
