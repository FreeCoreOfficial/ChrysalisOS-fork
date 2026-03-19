#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void user64_init_process(uint64_t image_end);
uint64_t user64_brk(uint64_t new_brk);
uint64_t user64_mmap(uint64_t addr, uint64_t len, int prot, int flags);
int user64_munmap(uint64_t addr, uint64_t len);

#ifdef __cplusplus
}
#endif
