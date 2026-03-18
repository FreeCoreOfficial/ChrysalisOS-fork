#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Internal syscall IDs derived from linux_syscalls.inc list order.
// These are NOT Linux syscall numbers; they are placeholders for the
// compatibility layer to map names while we wire a real ABI dispatcher.

enum linux_syscall_id {
#define SYSCALL(name_space, name) LINUX_SYS_##name,
#define STUB_SYSCALL(name) LINUX_SYS_##name,
#define UNIMPLEMENTED_SYSCALL(name) LINUX_SYS_##name,
#include "linux_syscalls.inc"
#undef UNIMPLEMENTED_SYSCALL
#undef STUB_SYSCALL
#undef SYSCALL
  LINUX_SYS_COUNT,
};

static const char *const linux_syscall_names[] = {
#define SYSCALL(name_space, name) #name,
#define STUB_SYSCALL(name) #name,
#define UNIMPLEMENTED_SYSCALL(name) #name,
#include "linux_syscalls.inc"
#undef UNIMPLEMENTED_SYSCALL
#undef STUB_SYSCALL
#undef SYSCALL
};

static inline const char *linux_syscall_name(enum linux_syscall_id id) {
  uint32_t idx = (uint32_t)id;
  if (idx >= (uint32_t)LINUX_SYS_COUNT)
    return "unknown";
  return linux_syscall_names[idx];
}

static inline uint32_t linux_syscall_count(void) {
  return (uint32_t)LINUX_SYS_COUNT;
}

#ifdef __cplusplus
}
#endif
