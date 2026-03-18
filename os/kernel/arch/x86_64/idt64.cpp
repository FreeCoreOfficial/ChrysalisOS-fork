#include "idt64.h"

struct idt64_entry {
  uint16_t offset_low;
  uint16_t selector;
  uint8_t ist;
  uint8_t type_attr;
  uint16_t offset_mid;
  uint32_t offset_high;
  uint32_t zero;
} __attribute__((packed));

struct idt64_ptr {
  uint16_t limit;
  uint64_t base;
} __attribute__((packed));

extern "C" void isr64_stub(void);

static idt64_entry g_idt[256];
static idt64_ptr g_idt_ptr;

static void set_idt_entry(int vec, void (*handler)(void)) {
  uint64_t addr = (uint64_t)handler;
  idt64_entry &e = g_idt[vec];
  e.offset_low = (uint16_t)(addr & 0xFFFF);
  e.selector = 0x08;
  e.ist = 0;
  e.type_attr = 0x8E;
  e.offset_mid = (uint16_t)((addr >> 16) & 0xFFFF);
  e.offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
  e.zero = 0;
}

void idt64_init(void) {
  for (int i = 0; i < 256; ++i) {
    set_idt_entry(i, isr64_stub);
  }

  g_idt_ptr.limit = (uint16_t)(sizeof(g_idt) - 1);
  g_idt_ptr.base = (uint64_t)&g_idt[0];

  asm volatile("lidt %0" : : "m"(g_idt_ptr));
}
