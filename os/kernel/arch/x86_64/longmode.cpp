#include "longmode.h"
#include "../i386/io.h"
#include "../hardware/msr.h"

#define MSR_EFER 0xC0000080u
#define EFER_LME (1u << 8)
#define EFER_LMA (1u << 10)

static void cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c,
                  uint32_t *d) {
  uint32_t eax, ebx, ecx, edx;
  asm volatile("cpuid"
               : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
               : "a"(leaf));
  if (a)
    *a = eax;
  if (b)
    *b = ebx;
  if (c)
    *c = ecx;
  if (d)
    *d = edx;
}

int cpu_has_long_mode(void) {
  uint32_t a, b, c, d;
  cpuid(0x80000000u, &a, &b, &c, &d);
  if (a < 0x80000001u)
    return 0;
  cpuid(0x80000001u, &a, &b, &c, &d);
  return (d & (1u << 29)) ? 1 : 0;
}

int cpu_is_long_mode(void) {
  if (!cpu_has_msr())
    return 0;
  uint32_t lo = 0, hi = 0;
  rdmsr(MSR_EFER, &lo, &hi);
  if (lo & EFER_LMA)
    return 1;
  if (lo & EFER_LME)
    return 1;
  return 0;
}
