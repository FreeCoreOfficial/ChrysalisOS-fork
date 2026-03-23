#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int execve_linux_x86_64_full(const char *filename, char *const argv[], char *const envp[]);
int exec64_from_module(void *start, uint64_t size, const char *image_path);

#ifdef __cplusplus
}
#endif
