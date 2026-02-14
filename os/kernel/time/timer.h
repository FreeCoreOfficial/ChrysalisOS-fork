#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void timer_init(uint32_t frequency);

/* raw tick counter */
uint64_t timer_ticks(void);
uint64_t timer_ticks_no_cli(void);

/* uptime helpers (SAFE, no 64-bit division) */
uint32_t timer_uptime_seconds(void);
uint32_t timer_uptime_ms(void);

/* sleep using PIT ticks */
void sleep(uint32_t ms);

#ifdef __cplusplus
}
#endif
