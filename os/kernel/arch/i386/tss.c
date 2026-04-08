#include "tss.h"
#include "gdt.h"
#include "../../string.h"

bool tss_loaded = false;
static struct tss_entry_struct tss_entry;

// Funcție externă din tss_flush.S
extern void tss_flush(void);

void tss_init(uint32_t kernel_stack) {
    uint32_t base = (uint32_t) &tss_entry;
    uint32_t limit = sizeof(tss_entry);

    // Setăm descriptorul TSS în GDT (index 6)
    // Base, Limit, Access(0xE9 = Present|Ring3|Executable|Accessed), Granularity
    gdt_set_gate(6, base, limit, 0xE9, 0x00);

    // Zero memory
    memset(&tss_entry, 0, sizeof(tss_entry));

    // Setăm stiva de kernel (pentru întreruperi venite din Ring 3)
    tss_entry.ss0 = 0x10;  // Kernel Data Segment
    tss_entry.esp0 = kernel_stack;

    // Setăm I/O Map Base la o valoare mai mare decât limita TSS pentru a dezactiva bitmap-ul I/O
    // (sau la sizeof(tss_entry) dacă vrem să fie gol)
    tss_entry.iomap_base = sizeof(tss_entry);

    // Încărcăm registrul TR (Task Register)
    tss_flush();

    tss_loaded = true;
}

void tss_set_stack(uint32_t kernel_stack) {
    tss_entry.esp0 = kernel_stack;
}