#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Execute one command line via command registry and capture terminal output. */
int cmd_exec_capture(const char *line, char *out, uint32_t out_cap);

#ifdef __cplusplus
}
#endif
