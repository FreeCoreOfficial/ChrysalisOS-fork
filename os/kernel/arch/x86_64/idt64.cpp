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

extern "C" void isr64_stub0(void);
extern "C" void isr64_stub1(void);
extern "C" void isr64_stub2(void);
extern "C" void isr64_stub3(void);
extern "C" void isr64_stub4(void);
extern "C" void isr64_stub5(void);
extern "C" void isr64_stub6(void);
extern "C" void isr64_stub7(void);
extern "C" void isr64_stub8(void);
extern "C" void isr64_stub9(void);
extern "C" void isr64_stub10(void);
extern "C" void isr64_stub11(void);
extern "C" void isr64_stub12(void);
extern "C" void isr64_stub13(void);
extern "C" void isr64_stub14(void);
extern "C" void isr64_stub15(void);
extern "C" void isr64_stub16(void);
extern "C" void isr64_stub17(void);
extern "C" void isr64_stub18(void);
extern "C" void isr64_stub19(void);
extern "C" void isr64_stub20(void);
extern "C" void isr64_stub21(void);
extern "C" void isr64_stub22(void);
extern "C" void isr64_stub23(void);
extern "C" void isr64_stub24(void);
extern "C" void isr64_stub25(void);
extern "C" void isr64_stub26(void);
extern "C" void isr64_stub27(void);
extern "C" void isr64_stub28(void);
extern "C" void isr64_stub29(void);
extern "C" void isr64_stub30(void);
extern "C" void isr64_stub31(void);

static idt64_entry g_idt[256];
static idt64_ptr g_idt_ptr;

static void set_idt_entry(int vec, void (*handler)(void)) {
  uint64_t addr = (uint64_t)handler;
  idt64_entry &e = g_idt[vec];
  e.offset_low = (uint16_t)(addr & 0xFFFF);
  e.selector = 0x08;
  e.ist = (vec == 8 || vec == 2) ? 1 : 0;
  e.type_attr = 0x8E;
  e.offset_mid = (uint16_t)((addr >> 16) & 0xFFFF);
  e.offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
  e.zero = 0;
}

void idt64_init(void) {
  set_idt_entry(0, isr64_stub0);
  set_idt_entry(1, isr64_stub1);
  set_idt_entry(2, isr64_stub2);
  set_idt_entry(3, isr64_stub3);
  set_idt_entry(4, isr64_stub4);
  set_idt_entry(5, isr64_stub5);
  set_idt_entry(6, isr64_stub6);
  set_idt_entry(7, isr64_stub7);
  set_idt_entry(8, isr64_stub8);
  set_idt_entry(9, isr64_stub9);
  set_idt_entry(10, isr64_stub10);
  set_idt_entry(11, isr64_stub11);
  set_idt_entry(12, isr64_stub12);
  set_idt_entry(13, isr64_stub13);
  set_idt_entry(14, isr64_stub14);
  set_idt_entry(15, isr64_stub15);
  set_idt_entry(16, isr64_stub16);
  set_idt_entry(17, isr64_stub17);
  set_idt_entry(18, isr64_stub18);
  set_idt_entry(19, isr64_stub19);
  set_idt_entry(20, isr64_stub20);
  set_idt_entry(21, isr64_stub21);
  set_idt_entry(22, isr64_stub22);
  set_idt_entry(23, isr64_stub23);
  set_idt_entry(24, isr64_stub24);
  set_idt_entry(25, isr64_stub25);
  set_idt_entry(26, isr64_stub26);
  set_idt_entry(27, isr64_stub27);
  set_idt_entry(28, isr64_stub28);
  set_idt_entry(29, isr64_stub29);
  set_idt_entry(30, isr64_stub30);
  set_idt_entry(31, isr64_stub31);

  g_idt_ptr.limit = (uint16_t)(sizeof(g_idt) - 1);
  g_idt_ptr.base = (uint64_t)&g_idt[0];

  asm volatile("lidt %0" : : "m"(g_idt_ptr));
}
