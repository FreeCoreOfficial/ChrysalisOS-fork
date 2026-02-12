/* kernel/sched/pcb.c */
#include "pcb.h"
#include "../terminal.h" /* păstrează pentru debug dacă vrei */
#include "scheduler.h"
#include <stddef.h>
#include <stdint.h>

/* simple internal zeroing helper — evităm <string.h> în build freestanding */
static void zero_mem(void *dst, size_t n) {
  unsigned char *p = (unsigned char *)dst;
  while (n--)
    *p++ = 0;
}

static pcb_t pcbs[MAX_TASKS];
static int current_tid = -1;

void pcb_init_all(void) {
  for (int i = 0; i < MAX_TASKS; ++i) {
    zero_mem(&pcbs[i], sizeof(pcb_t));
    pcbs[i].pid = i;
  }
  current_tid = -1;
}

/* internal helper to prepare initial stack for a fresh task
 * The stack is prepared so that when we load esp and pop registers
 * we return into task_trampoline which calls the real entry.
 */
extern void task_trampoline(void);

static uint32_t *init_stack_for(pcb_t *p) {
  /* start at top of stack (grow down) */
  uint32_t *sp = (uint32_t *)(p->kstack + KSTACK_SIZE);

  /* push a fake return address so 'ret' goes to task_trampoline */
  *(--sp) = (uint32_t)task_trampoline;

  /* reserve space that pushad/popad will touch (8 regs) */
  for (int i = 0; i < 8; ++i) {
    *(--sp) = 0x0; /* EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI - values don't matter
                      initially */
  }

  return sp;
}

int pcb_create(task_fn_t entry, void *arg) {
  for (int i = 0; i < MAX_TASKS; ++i) {
    if (pcbs[i].state == TASK_UNUSED) {
      pcbs[i].state = TASK_READY;
      /* Note: in new task_t, entry and arg are not fields,
         we prepare them on the stack. */
      pcbs[i].kstack_ptr = init_stack_for(&pcbs[i]);
      /* manually push entry & arg if needed (init_stack_for prepares
       * trampoline) */
      /* Actually task_trampoline needs them? Or we can use the task_t push
       * helper */
      pcbs[i].ticks_remaining = 0;
      return pcbs[i].pid;
    }
  }
  return -1; /* no slot */
}

pcb_t *pcb_get_current(void) { return current_task; }

/* small helpers used by scheduler.c */
pcb_t *_pcb_get_by_index(int idx) {
  if (idx < 0 || idx >= MAX_TASKS)
    return NULL;
  return &pcbs[idx];
}

int _pcb_count(void) {
  int c = 0;
  for (int i = 0; i < MAX_TASKS; ++i)
    if (pcbs[i].state != TASK_UNUSED)
      c++;
  return c;
}

/* expose a way for scheduler to set current */
void _pcb_set_current(int idx) { current_tid = idx; }
int _pcb_get_current_idx(void) { return current_tid; }
