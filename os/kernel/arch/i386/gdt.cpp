// FILE: kernel/arch/i386/gdt.cpp
#include "gdt.h"
#include <stdint.h>
#define GDT_USER_CODE (3*8)
#define GDT_USER_DATA (4*8)




extern "C" void gdt_flush(uint32_t);


struct gdt_entry {
uint16_t limit_low;
uint16_t base_low;
uint8_t base_middle;
uint8_t access;
uint8_t granularity;
uint8_t base_high;
} __attribute__((packed));


static gdt_entry gdt[8];
static gdt_ptr_t gp;
static const int GDT_TLS_INDEX = 5;
static const int GDT_TSS_INDEX = 6;


/*
* Nota: eliminam 'static' astfel incat functia sa fie vizibila din tss.c
* si sa corespunda cu prototipul din gdt.h
*/
void gdt_set_gate(int num, uint32_t base, uint32_t limit,
uint8_t access, uint8_t gran)
{
gdt[num].base_low = base & 0xFFFF;
gdt[num].base_middle = (base >> 16) & 0xFF;
gdt[num].base_high = (base >> 24) & 0xFF;


gdt[num].limit_low = limit & 0xFFFF;
gdt[num].granularity = (limit >> 16) & 0x0F;


gdt[num].granularity |= gran & 0xF0;
gdt[num].access = access;
}


extern "C" void gdt_init()
{
gp.limit = (sizeof(gdt_entry) * 8) - 1;
gp.base = (uint32_t)&gdt;

// Zero GDT
for (int i = 0; i < 8; i++) {
    gdt_set_gate(i, 0, 0, 0, 0);
}

// 1) Kernel code (Ring 0)
gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);


// 2) Kernel data (Ring 0)
gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);


// 3) User code (Ring 3)
gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);


// 4) User data (Ring 3)
gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);

// 5) User TLS (Ring 3 data)
gdt_set_gate(GDT_TLS_INDEX, 0, 0xFFFFFFFF, 0xF2, 0xCF);


gdt_flush((uint32_t)&gp);
}

extern "C" void gdt_get_ptr(gdt_ptr_t* out) {
    asm volatile("sgdt %0" : "=m"(*out));
}

extern "C" void gdt_set_tls_base(uint32_t base) {
    gdt_set_gate(GDT_TLS_INDEX, base, 0xFFFFFFFF, 0xF2, 0xCF);
}

extern "C" uint16_t gdt_get_tls_selector(void) {
    return (uint16_t)((GDT_TLS_INDEX * 8) | 0x3);
}
