// FILE: kernel/arch/i386/gdt.h
#pragma once
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif


/* GDT structs (public names) */
struct GDTEntry {
uint16_t limit_low;
uint16_t base_low;
uint8_t base_middle;
uint8_t access;
uint8_t granularity;
uint8_t base_high;
} __attribute__((packed));


typedef struct {
uint16_t limit;
uint32_t base;
} __attribute__((packed)) gdt_ptr_t;


/* GDT API public */
void gdt_flush(uint32_t gdt_ptr_addr);
void gdt_init(void);


/* Expunem gdt_set_gate pentru a putea adauga TSS din tss.c */
void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);

/* Exportă GDTR-ul actual */
void gdt_get_ptr(gdt_ptr_t* out);
void gdt_set_tls_base(uint32_t base);
uint16_t gdt_get_tls_selector(void);


#ifdef __cplusplus
}
#endif
