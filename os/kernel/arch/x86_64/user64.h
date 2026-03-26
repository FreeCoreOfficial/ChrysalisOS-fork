#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void user64_enter(uint64_t rip, uint64_t rsp, uint64_t rflags, uint64_t fs_base, uint64_t gs_base);

#ifdef __cplusplus
}
#endif
