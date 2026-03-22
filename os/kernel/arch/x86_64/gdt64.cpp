#include "gdt64.h"
#include <string.h>

struct gdt64_entry {
  uint16_t limit_low;
  uint16_t base_low;
  uint8_t base_mid;
  uint8_t access;
  uint8_t gran;
  uint8_t base_high;
} __attribute__((packed));

struct gdt64_tss_entry {
  uint16_t limit_low;
  uint16_t base_low;
  uint8_t base_mid;
  uint8_t access;
  uint8_t gran;
  uint8_t base_high;
  uint32_t base_upper;
  uint32_t reserved;
} __attribute__((packed));

struct gdt64_ptr {
  uint16_t limit;
  uint64_t base;
} __attribute__((packed));

struct tss64 {
  uint32_t reserved0;
  uint64_t rsp0;
  uint64_t rsp1;
  uint64_t rsp2;
  uint64_t reserved1;
  uint64_t ist1;
  uint64_t ist2;
  uint64_t ist3;
  uint64_t ist4;
  uint64_t ist5;
  uint64_t ist6;
  uint64_t ist7;
  uint64_t reserved2;
  uint16_t reserved3;
  uint16_t iomap_base;
} __attribute__((packed));

struct gdt64_table {
  gdt64_entry entries[5];
  gdt64_tss_entry tss;
} __attribute__((packed));

static gdt64_table g_gdt;
static tss64 g_tss;
static gdt64_ptr g_gdt_ptr;
extern "C" uint64_t g_tss64_rsp0 = 0;

static void gdt_set_entry(gdt64_entry &e, uint32_t base, uint32_t limit,
                          uint8_t access, uint8_t gran) {
  e.limit_low = (uint16_t)(limit & 0xFFFF);
  e.base_low = (uint16_t)(base & 0xFFFF);
  e.base_mid = (uint8_t)((base >> 16) & 0xFF);
  e.access = access;
  e.gran = (uint8_t)(((limit >> 16) & 0x0F) | (gran & 0xF0));
  e.base_high = (uint8_t)((base >> 24) & 0xFF);
}

static void tss_set_desc(gdt64_tss_entry &e, uint64_t base, uint32_t limit) {
  e.limit_low = (uint16_t)(limit & 0xFFFF);
  e.base_low = (uint16_t)(base & 0xFFFF);
  e.base_mid = (uint8_t)((base >> 16) & 0xFF);
  e.access = 0x89; /* present, type=9 (available 64-bit TSS) */
  e.gran = (uint8_t)((limit >> 16) & 0x0F);
  e.base_high = (uint8_t)((base >> 24) & 0xFF);
  e.base_upper = (uint32_t)((base >> 32) & 0xFFFFFFFFu);
  e.reserved = 0;
}

void tss64_set_rsp0(uint64_t rsp0) {
  g_tss.rsp0 = rsp0;
  g_tss64_rsp0 = rsp0;
}
void tss64_set_ist1(uint64_t rsp1) { g_tss.ist1 = rsp1; }
uint64_t tss64_get_rsp0(void) { return g_tss.rsp0; }
uint64_t tss64_get_ist1(void) { return g_tss.ist1; }

void gdt64_init(uint64_t rsp0) {
  memset(&g_gdt, 0, sizeof(g_gdt));
  memset(&g_tss, 0, sizeof(g_tss));

  /*
   * GDT Layout for x86_64
   * 
   * This layout is STRICTLY required by the SYSCALL/SYSRET instructions.
   * STAR MSR bits [63:48] defines the base selector for SYSRET.
   * 
   * Index 0 (0x00): Null descriptor
   * Index 1 (0x08): Kernel Code (64-bit) (L=1, D=0)
   * Index 2 (0x10): Kernel Data (L=0, D=0) - automatically loaded as SS by SYSCALL
   * Index 3 (0x18): User Data (L=0, D=0) - loaded as SS by SYSRET (STAR[63:48] + 8)
   * Index 4 (0x20): User Code (64-bit) (L=1, D=0) - loaded as CS by SYSRET (STAR[63:48] + 16)
   */
  gdt_set_entry(g_gdt.entries[0], 0, 0, 0, 0);
  gdt_set_entry(g_gdt.entries[1], 0, 0xFFFFF, 0x9A, 0xA0); /* kernel code (L=1) */
  gdt_set_entry(g_gdt.entries[2], 0, 0xFFFFF, 0x92, 0x80); /* kernel data (L=0, D=0) */
  gdt_set_entry(g_gdt.entries[3], 0, 0xFFFFF, 0xF2, 0x80); /* user data (L=0, D=0) */
  gdt_set_entry(g_gdt.entries[4], 0, 0xFFFFF, 0xFA, 0xA0); /* user code (L=1) */

  g_tss.iomap_base = sizeof(g_tss);
  tss64_set_rsp0(rsp0);
  tss_set_desc(g_gdt.tss, (uint64_t)(uintptr_t)&g_tss,
               (uint32_t)(sizeof(g_tss) - 1));

  g_gdt_ptr.limit = (uint16_t)(sizeof(g_gdt) - 1);
  g_gdt_ptr.base = (uint64_t)(uintptr_t)&g_gdt;

  asm volatile("lgdt %0" : : "m"(g_gdt_ptr));
  asm volatile("ltr %0" : : "r"((uint16_t)0x28));
}
