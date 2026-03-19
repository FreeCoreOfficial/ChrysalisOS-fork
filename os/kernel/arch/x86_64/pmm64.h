#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void pmm64_init(uint64_t mb_info_addr, uint64_t reserved_end);
uint64_t pmm64_alloc_frame(void);
int pmm64_is_ready(void);

#ifdef __cplusplus
}
#endif
