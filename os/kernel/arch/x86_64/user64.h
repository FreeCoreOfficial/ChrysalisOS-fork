#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct user64_context {
  uint64_t rip;
  uint64_t rsp;
  uint64_t rflags;
  uint64_t fs_base;
  uint64_t gs_base;
};

void user64_enter(struct user64_context *ctx);

#ifdef __cplusplus
}
#endif
