#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int exec64_from_module(void *start, uint64_t size, const char *image_path);

#ifdef __cplusplus
}
#endif
