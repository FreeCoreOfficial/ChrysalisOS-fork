#include "syscall64.h"
#include "../../hardware/msr.h"

#define MSR_EFER 0xC0000080u
#define MSR_STAR 0xC0000081u
#define MSR_LSTAR 0xC0000082u
#define MSR_FMASK 0xC0000084u

extern "C" void syscall64_entry(void);

void syscall64_init(void) {
  /* Enable SCE in EFER */
  uint32_t lo = 0, hi = 0;
  rdmsr(MSR_EFER, &lo, &hi);
  lo |= 1u; /* SCE */
  wrmsr(MSR_EFER, lo, hi);

  /* STAR: kernel CS = 0x08, user SS = 0x18, user CS = 0x20 */
  uint64_t star = ((uint64_t)0x0010 << 48) | ((uint64_t)0x0008 << 32);
  wrmsr(MSR_STAR, (uint32_t)(star & 0xFFFFFFFFu),
        (uint32_t)(star >> 32));

  /* LSTAR: syscall entry point */
  uint64_t lstar = (uint64_t)(unsigned long long)syscall64_entry;
  wrmsr(MSR_LSTAR, (uint32_t)(lstar & 0xFFFFFFFFu),
        (uint32_t)(lstar >> 32));

  /* FMASK: clear IF on entry */
  wrmsr(MSR_FMASK, 1u << 9, 0);
}

uint64_t syscall64_dispatch(uint64_t num, uint64_t a1, uint64_t a2,
                            uint64_t a3, uint64_t a4, uint64_t a5,
                            uint64_t a6) {
  (void)num;
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return (uint64_t)-38; /* ENOSYS */
}
