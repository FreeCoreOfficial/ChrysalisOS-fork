#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void gdt64_init(uint64_t rsp0);
void tss64_set_rsp0(uint64_t rsp0);
void tss64_set_ist1(uint64_t rsp1);
uint64_t tss64_get_rsp0(void);
uint64_t tss64_get_ist1(void);

extern uint64_t g_tss64_rsp0;

#ifdef __cplusplus
}
#endif
