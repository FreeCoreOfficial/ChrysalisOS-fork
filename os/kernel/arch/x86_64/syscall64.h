#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void syscall64_init(void);
uint64_t syscall64_dispatch(uint64_t num, uint64_t a1, uint64_t a2,
                            uint64_t a3, uint64_t a4, uint64_t a5,
                            uint64_t a6);

#ifdef __cplusplus
}
#endif
