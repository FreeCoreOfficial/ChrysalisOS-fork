#include "task64.h"
#include "../mem/kmalloc.h"
#include "../hardware/msr.h"
#include "../arch/x86_64/gdt64.h"
#include "../string.h"
#include <stddef.h>

extern "C" void switch64(uint64_t *old_rsp, uint64_t *new_rsp);

static task64_t *g_task64_list = nullptr;
static task64_t *g_task64_current = nullptr;
static uint64_t g_task64_next_id = 1;

static const uint64_t k_task64_stack_size = 16 * 1024;
static const uint32_t MSR_FS_BASE = 0xC0000100u;
static const uint32_t MSR_GS_BASE = 0xC0000101u;
static const uint32_t MSR_KERNEL_GS_BASE = 0xC0000102u;

static inline void wrmsr64(uint32_t msr, uint64_t value) {
  wrmsr(msr, (uint32_t)(value & 0xFFFFFFFFu),
        (uint32_t)(value >> 32));
}

static inline void task64_set_kernel_gs(task64_t *t) {
  if (!t)
    return;
  wrmsr64(MSR_KERNEL_GS_BASE, (uint64_t)(uintptr_t)&t->gs);
  wrmsr64(MSR_FS_BASE, t->gs.fs_base);
  tss64_set_rsp0(t->gs.kernel_stack);
  /* NOTE: do NOT overwrite IST1 here. IST1 is a dedicated double-fault
   * stack set once in kernel_main64 via tss64_set_ist1(). Clobbering it
   * per-task causes #DF to land on the same stack as the primary fault
   * handler, producing a corrupted/shifted exception frame. */
}

static void task64_idle(void *arg) {
  (void)arg;
  for (;;) {
    asm volatile("hlt");
  }
}

__attribute__((noreturn)) static void task64_trampoline(void) {
  task64_t *t = g_task64_current;
  if (!t || !t->entry) {
    for (;;)
      asm volatile("hlt");
  }
  t->entry(t->arg);
  t->state = TASK64_ZOMBIE;
  for (;;)
    asm volatile("hlt");
}

static uint64_t *task64_init_stack(uint8_t *stack_base) {
  uint8_t *top = stack_base + k_task64_stack_size;
  top = (uint8_t *)((uintptr_t)top & ~0xFULL);
  uint64_t *sp = (uint64_t *)top;

  *(--sp) = (uint64_t)(uintptr_t)task64_trampoline; /* return address */
  *(--sp) = 0; /* rbx */
  *(--sp) = 0; /* rbp */
  *(--sp) = 0; /* r12 */
  *(--sp) = 0; /* r13 */
  *(--sp) = 0; /* r14 */
  *(--sp) = 0; /* r15 */
  return sp;
}

void task64_init(void) {
  g_task64_list = nullptr;
  g_task64_current = nullptr;
  g_task64_next_id = 1;
  wrmsr64(MSR_GS_BASE, 0);
  task64_create("idle", task64_idle, nullptr);
}

task64_t *task64_create(const char *name, void (*entry)(void *), void *arg) {
  task64_t *t = (task64_t *)kmalloc(sizeof(task64_t));
  if (!t)
    return nullptr;
  memset(t, 0, sizeof(*t));
  t->id = g_task64_next_id++;
  t->entry = entry;
  t->arg = arg;
  t->state = TASK64_READY;
  if (name && *name) {
    strncpy(t->name, name, sizeof(t->name) - 1);
    t->name[sizeof(t->name) - 1] = 0;
  } else {
    strcpy(t->name, "task");
  }

  uint8_t *stack = (uint8_t *)kmalloc(k_task64_stack_size);
  if (!stack) {
    kfree(t);
    return nullptr;
  }
  t->rsp = (uint64_t)(uintptr_t)task64_init_stack(stack);
  t->gs.kernel_stack = (uint64_t)(uintptr_t)(stack + k_task64_stack_size);
  t->gs.user_stack = 0;
  t->gs.fs_base = 0;
  t->exe_path[0] = 0;
  t->uid = t->gid = 0;
  t->euid = t->egid = 0;

  t->user_brk_start = 0;
  t->user_brk_end = 0;
  t->user_mmap_base = 0;

  if (!g_task64_list) {
    g_task64_list = t;
    t->next = t;
  } else {
    t->next = g_task64_list->next;
    g_task64_list->next = t;
  }

  return t;
}

void task64_start(task64_t *first) {
  if (!first)
    return;
  g_task64_current = first;
  first->state = TASK64_RUNNING;
  task64_set_kernel_gs(first);
  uint64_t dummy_rsp = 0;
  switch64(&dummy_rsp, &first->rsp);
  for (;;)
    asm volatile("hlt");
}

void task64_yield(void) {
  if (!g_task64_current || !g_task64_current->next)
    return;

  task64_t *prev = g_task64_current;
  task64_t *next = prev->next;

  int guard = 0;
  while (next != prev && guard++ < 64) {
    if (next->state == TASK64_READY || next->state == TASK64_RUNNING)
      break;
    next = next->next;
  }
  if (next == prev)
    return;

  g_task64_current = next;
  next->state = TASK64_RUNNING;
  if (prev->state == TASK64_RUNNING)
    prev->state = TASK64_READY;
  switch64(&prev->rsp, &next->rsp);
  task64_set_kernel_gs(g_task64_current);
}

task64_t *task64_current(void) { return g_task64_current; }

void task64_set_user_stack(uint64_t user_rsp) {
  if (!g_task64_current)
    return;
  g_task64_current->gs.user_stack = user_rsp;
  task64_set_kernel_gs(g_task64_current);
}
