#include "pic.h"
#include "../arch/i386/io.h"
#include <stdint.h>

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

#define ICW1_INIT 0x10
#define ICW1_ICW4 0x01
#define ICW4_8086 0x01

extern "C" void pic_remap()
{
    /* Save mask */
    uint8_t a1 = inb(PIC1_DATA);
    uint8_t a2 = inb(PIC2_DATA);

    /* Disable interrupts during remap to avoid inconsistent state */
    asm volatile("cli");

    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();

    outb(PIC1_DATA, 0x20); // IRQ 0–7 → 32–39
    io_wait();
    outb(PIC2_DATA, 0x28); // IRQ 8–15 → 40–47
    io_wait();

    outb(PIC1_DATA, 4);
    io_wait();
    outb(PIC2_DATA, 2);
    io_wait();

    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    /* Restore masks */
    outb(PIC1_DATA, a1);
    outb(PIC2_DATA, a2);
    
    /* Note: we don't 'sti' here because the caller might want to keep them disabled */
}

// C linkage pentru ASM / C callers
extern "C" void pic_send_eoi(uint8_t irq)
{
    /* If the IRQ came from the slave PIC (IRQ 8..15), ack the slave first */
    if (irq >= 8) {
        outb(0xA0, 0x20);
    }
    /* Always ack the master PIC */
    outb(0x20, 0x20);
}