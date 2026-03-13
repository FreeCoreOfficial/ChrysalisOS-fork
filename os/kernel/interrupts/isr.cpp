// kernel/interrupts/isr.cpp
#include "isr.h"
#include "../arch/i386/idt.h"
#include "../drivers/serial.h" // Pentru debug pe serial
#include "../include/task.h"
#include "../panic.h"
#include "../terminal.h"
#include <stdint.h>

extern "C" {
/* Declarații externe pentru stub-urile ASM (isr0..isr31) */
void isr0();
void isr1();
void isr2();
void isr3();
void isr4();
void isr5();
void isr6();
void isr7();
void isr8();
void isr9();
void isr10();
void isr11();
void isr12();
void isr13();
void isr14();
void isr15();
void isr16();
void isr17();
void isr18();
void isr19();
void isr20();
void isr21();
void isr22();
void isr23();
void isr24();
void isr25();
void isr26();
void isr27();
void isr28();
void isr29();
void isr30();
void isr31();
}

/* Instalează handlerele de excepții în IDT (0-31) */
extern "C" void isr_install() {
  idt_set_gate(0, (uint32_t)(uintptr_t)isr0, 0x08, 0x8E);
  idt_set_gate(1, (uint32_t)(uintptr_t)isr1, 0x08, 0x8E);
  idt_set_gate(2, (uint32_t)(uintptr_t)isr2, 0x08, 0x8E);
  idt_set_gate(3, (uint32_t)(uintptr_t)isr3, 0x08, 0x8E);
  idt_set_gate(4, (uint32_t)(uintptr_t)isr4, 0x08, 0x8E);
  idt_set_gate(5, (uint32_t)(uintptr_t)isr5, 0x08, 0x8E);
  idt_set_gate(6, (uint32_t)(uintptr_t)isr6, 0x08, 0x8E);
  idt_set_gate(7, (uint32_t)(uintptr_t)isr7, 0x08, 0x8E);
  idt_set_gate(8, (uint32_t)(uintptr_t)isr8, 0x08, 0x8E);
  idt_set_gate(9, (uint32_t)(uintptr_t)isr9, 0x08, 0x8E);
  idt_set_gate(10, (uint32_t)(uintptr_t)isr10, 0x08, 0x8E);
  idt_set_gate(11, (uint32_t)(uintptr_t)isr11, 0x08, 0x8E);
  idt_set_gate(12, (uint32_t)(uintptr_t)isr12, 0x08, 0x8E);
  idt_set_gate(13, (uint32_t)(uintptr_t)isr13, 0x08, 0x8E);
  idt_set_gate(14, (uint32_t)(uintptr_t)isr14, 0x08, 0x8E);
  idt_set_gate(15, (uint32_t)(uintptr_t)isr15, 0x08, 0x8E);
  idt_set_gate(16, (uint32_t)(uintptr_t)isr16, 0x08, 0x8E);
  idt_set_gate(17, (uint32_t)(uintptr_t)isr17, 0x08, 0x8E);
  idt_set_gate(18, (uint32_t)(uintptr_t)isr18, 0x08, 0x8E);
  idt_set_gate(19, (uint32_t)(uintptr_t)isr19, 0x08, 0x8E);
  idt_set_gate(20, (uint32_t)(uintptr_t)isr20, 0x08, 0x8E);
  idt_set_gate(21, (uint32_t)(uintptr_t)isr21, 0x08, 0x8E);
  idt_set_gate(22, (uint32_t)(uintptr_t)isr22, 0x08, 0x8E);
  idt_set_gate(23, (uint32_t)(uintptr_t)isr23, 0x08, 0x8E);
  idt_set_gate(24, (uint32_t)(uintptr_t)isr24, 0x08, 0x8E);
  idt_set_gate(25, (uint32_t)(uintptr_t)isr25, 0x08, 0x8E);
  idt_set_gate(26, (uint32_t)(uintptr_t)isr26, 0x08, 0x8E);
  idt_set_gate(27, (uint32_t)(uintptr_t)isr27, 0x08, 0x8E);
  idt_set_gate(28, (uint32_t)(uintptr_t)isr28, 0x08, 0x8E);
  idt_set_gate(29, (uint32_t)(uintptr_t)isr29, 0x08, 0x8E);
  idt_set_gate(30, (uint32_t)(uintptr_t)isr30, 0x08, 0x8E);
  idt_set_gate(31, (uint32_t)(uintptr_t)isr31, 0x08, 0x8E);
}

extern "C" void isr_handler(registers_t *r) {
  // Print basic info
  terminal_writestring("\n[ISR] CPU Exception:\n");
  terminal_writestring("Interrupt: ");
  // print decimal int_no
  {
    uint32_t v = r->int_no;
    // simple itoa decimal
    char buf[12];
    int idx = 0;
    if (v == 0) {
      terminal_putchar('0');
    } else {
      while (v) {
        buf[idx++] = '0' + (v % 10);
        v /= 10;
      }
      while (idx--)
        terminal_putchar(buf[idx]);
    }
    terminal_writestring("\n");
  }

  terminal_writestring("Error code: ");
  // hex print 32-bit
  {
    const char *hex = "0123456789ABCDEF";
    for (int i = 7; i >= 0; --i) {
      terminal_putchar(hex[(r->err_code >> (i * 4)) & 0xF]);
    }
    terminal_writestring("\n");
  }

  // Optionally dump some registers (EIP/CS/EFLAGS)
  terminal_writestring("EIP: ");
  {
    const char *hex = "0123456789ABCDEF";
    uint32_t v = r->eip;
    for (int i = 7; i >= 0; --i)
      terminal_putchar(hex[(v >> (i * 4)) & 0xF]);
  }
  terminal_writestring("\n");

  // Dacă este Page Fault (14), afișăm CR2
  if (r->int_no == 14) {
    uint32_t cr2;
    asm volatile("mov %%cr2, %0" : "=r"(cr2));
    terminal_printf("CR2 (Fault Addr): 0x%x\n", cr2);
    serial_printf("[ISR] Page Fault CR2=0x%x EIP=0x%x Err=0x%x\n", cr2, r->eip,
                  r->err_code);
  }

  /* Check if exception came from user mode (CS & 3 == 3) */
  int user_mode = ((r->cs & 3) == 3);

  /* Always print current task info for debugging */
  if (current_task) {
    terminal_printf("[ISR] Exception in PID=%d (State=%d)\n", current_task->pid,
                    current_task->state);
    terminal_printf("[ISR] Task name: %s\n", current_task->name);
    serial_printf("[ISR] Exception in PID=%d (State=%d)\n", current_task->pid,
                  current_task->state);
    serial_printf("[ISR] Task name: %s\n", current_task->name);
    if (current_task->last_syscall != 0) {
      terminal_printf("[ISR] Last syscall=%d a1=0x%x a2=0x%x a3=0x%x\n",
                      current_task->last_syscall,
                      current_task->last_syscall_a1,
                      current_task->last_syscall_a2,
                      current_task->last_syscall_a3);
      serial_printf("[ISR] Last syscall=%d a1=0x%x a2=0x%x a3=0x%x\n",
                    current_task->last_syscall, current_task->last_syscall_a1,
                    current_task->last_syscall_a2,
                    current_task->last_syscall_a3);
    }
  } else {
    terminal_writestring("[ISR] Exception with NO current_task.\n");
  }

  /* Heuristic: If we are in Ring 0 but EIP is in User Space (0x08000000+)
     or Low Memory (<1MB) and PID > 1, it's a user app crashing in Ring 0
     fallback. */
  if (!user_mode && current_task && current_task->pid > 1) {
    /* If EIP is clearly user code OR clearly garbage (like < 1MB) */
    /* Note: Kernel starts at 1MB (0x100000). So < 0x100000 is invalid for
     * kernel code too */
    if (r->eip >= 0x08000000 || r->eip < 0x00100000) {
      user_mode = 1;
      terminal_writestring("[ISR] Heuristic: Detected User-in-Ring0 crash (Low "
                           "EIP or User EIP).\n");
    }

    /* Also check for Page Fault CR2 in user space */
    if (r->int_no == 14) {
      uint32_t cr2;
      asm volatile("mov %%cr2, %0" : "=r"(cr2));
      if (cr2 >= 0x08000000) {
        user_mode = 1;
        terminal_writestring("[ISR] Heuristic: Detected User-in-Ring0 crash "
                             "(CR2 in User Space).\n");
      }
    }
  }

  if (user_mode) {
    terminal_printf("[ISR] Exception in task PID=%d. Terminating.\n",
                    current_task ? current_task->pid : -1);
    serial_printf("[ISR] User Fault: killing task. EIP=0x%x\n", r->eip);

    /* Terminate current task */
    task_exit(-1);
    /* task_exit does not return */
  }

  // Call panic (this should never return)
  terminal_writestring("\n*** KERNEL PANIC: Unhandled CPU exception ***\n");
  panic_ex("Unhandled CPU exception", r->eip, r->cs, r->eflags);
}
