#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct task;

void user32_init_process(struct task *t, uint32_t image_end);
uint32_t user32_brk(struct task *t, uint32_t new_brk);
uint32_t user32_mmap(struct task *t, uint32_t addr, uint32_t len, int prot,
                     int flags);
int user32_munmap(struct task *t, uint32_t addr, uint32_t len);
int user32_mprotect(struct task *t, uint32_t addr, uint32_t len, int prot);

#ifdef __cplusplus
}
#endif
