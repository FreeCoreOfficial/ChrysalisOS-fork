#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int linux_syscall_dispatch_i386(uint32_t num, uint32_t a1, uint32_t a2,
                                uint32_t a3, uint32_t a4, uint32_t a5,
                                uint32_t a6);

#ifdef __cplusplus
}
#endif
