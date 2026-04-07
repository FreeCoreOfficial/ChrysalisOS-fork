#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint64_t ds;
  uint64_t es;
  uint64_t r15;
  uint64_t r14;
  uint64_t r13;
  uint64_t r12;
  uint64_t r11;
  uint64_t r10;
  uint64_t r9;
  uint64_t r8;
  uint64_t rbp;
  uint64_t rdi;
  uint64_t rsi;
  uint64_t rdx;
  uint64_t rcx;
  uint64_t rbx;
  uint64_t rax;
} syscall64_state_t;

void syscall64_init(void);
void syscall64_set_linux_abi(int enabled);
uint64_t syscall64_dispatch(syscall64_state_t *state, uint64_t num, uint64_t a1, uint64_t a2,
                            uint64_t a3, uint64_t a4, uint64_t a5,
                            uint64_t a6);
uint64_t syscall64_dispatch_native(syscall64_state_t *state, uint64_t num, uint64_t a1,
                                   uint64_t a2, uint64_t a3, uint64_t a4,
                                   uint64_t a5, uint64_t a6);
void __syscall_handler(syscall64_state_t *state);
syscall64_state_t *syscall64_get_state(void);
struct file *syscall64_get_file(int fd);
void syscall64_set_file(int fd, struct file *f);
uint8_t syscall64_get_fd_flags(int fd);
void syscall64_set_fd_flags(int fd, uint8_t flags);
int syscall64_resolve_path(const char *path, char *out, uint64_t out_size);
void syscall64_prepare_exec_transition(void);

#ifdef __cplusplus
}
#endif
