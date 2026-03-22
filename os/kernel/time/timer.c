// kernel/time/timer.c
#include "timer.h"
#include "../arch/i386/io.h"
#include "../include/task.h"
#include "../interrupts/irq.h"
#include "../interrupts/isr.h"
#include <stdint.h>

/* tick counter (updated from IRQ) */
static volatile uint64_t ticks = 0;
static uint32_t tick_hz = 100;

/* IRQ0 handler */
static void timer_irq_handler(registers_t *regs) {
  (void)regs;
  ticks++;
}

void timer_init(uint32_t frequency) {
  if (frequency == 0)
    return;

  tick_hz = frequency;
  ticks = 0;

#ifndef __x86_64__
  irq_install_handler(0, timer_irq_handler);
#endif

  // Configurare Hardware PIT (Programmable Interval Timer)
  // Frecvența de bază: 1193180 Hz. Divizor = 1193180 / Hz dorit.
  uint32_t divisor = 1193180 / frequency;
  outb(0x43, 0x36); // 0x36 = Channel 0, Access lo/hi byte, Mode 3 (Square Wave)
  outb(0x40, (uint8_t)(divisor & 0xFF));        // Low byte
  outb(0x40, (uint8_t)((divisor >> 8) & 0xFF)); // High byte
}

uint64_t timer_ticks(void) {
  uint64_t ret;
  asm volatile("cli");
  ret = ticks;
  asm volatile("sti");
  return ret;
}

uint64_t timer_ticks_no_cli(void) { return ticks; }

uint32_t timer_uptime_seconds(void) {
  if (tick_hz == 0)
    return 0;
  return (uint32_t)ticks / tick_hz;
}

uint32_t timer_uptime_ms(void) {
  if (tick_hz == 0)
    return 0;
  return ((uint32_t)ticks * 1000) / tick_hz;
}
#ifdef __x86_64__
#include "../sched/task64.h"
#endif

void sleep(uint32_t ms) {
  if (tick_hz == 0)
    return;

  uint32_t wait_ticks = (ms * tick_hz) / 1000;
  if (wait_ticks == 0)
    wait_ticks = 1;

#ifdef __x86_64__
  task64_t *curr = task64_current();
  if (curr) {
      curr->sleep_until = timer_ticks() + wait_ticks;
      curr->state = TASK64_SLEEPING;
      task64_yield();
  } else {
#else
  if (current_task) {
    current_task->sleep_until = timer_ticks() + wait_ticks;
    current_task->state = TASK_SLEEPING;
    yield();
  } else {
#endif
    /* Fallback for when scheduler not initialized */
    uint64_t start = timer_ticks();
    while ((timer_ticks() - start) < wait_ticks) {
      asm volatile("pause");
    }
  }
}
