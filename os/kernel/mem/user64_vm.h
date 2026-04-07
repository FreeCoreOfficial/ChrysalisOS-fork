#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USER64_VMA_FLAG_DEVICE 0x10000000
#define USER64_VMA_FLAG_FILE   0x20000000

void user64_init_process(uint64_t image_end);
void user64_reset_process(void);
int user64_register_vma(uint64_t start, uint64_t end, int prot, int flags);
uint64_t user64_brk(uint64_t new_brk);
uint64_t user64_mmap(uint64_t addr, uint64_t len, int prot, int flags);
uint64_t user64_mmap_phys(uint64_t addr, uint64_t len, int prot, int flags,
                          uint64_t phys_base);
uint64_t user64_mremap(uint64_t old_addr, uint64_t old_len, uint64_t new_len,
                       uint64_t flags);
int user64_munmap(uint64_t addr, uint64_t len);
int user64_mprotect(uint64_t addr, uint64_t len, int prot);
uint64_t user64_get_sigtramp(void);
void user64_debug_dump_vmas(const char *tag);

#ifdef __cplusplus
}
#endif
