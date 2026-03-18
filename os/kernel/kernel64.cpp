#include <stdint.h>

extern "C" void idt64_init(void);
extern "C" void isr64_handler(void);
extern "C" void paging64_init(void);
extern "C" void syscall64_init(void);
extern "C" int exec64_from_module(void *start, unsigned long long size);

extern "C" void kernel_main64(unsigned long long magic,
                              unsigned long long info) {
  (void)magic;
  (void)info;

  volatile unsigned short *vga = (unsigned short *)0xB8000;
  const char *msg = "ChrysalisOS 64-bit kernel (prototype)";
  for (int i = 0; msg[i]; ++i) {
    vga[i] = (unsigned short)((0x0F << 8) | msg[i]);
  }

  paging64_init();
  idt64_init();
  syscall64_init();

  if (info) {
    uint8_t *mb = (uint8_t *)(unsigned long long)info;
    uint32_t total = *(uint32_t *)mb;
    uint32_t off = 8;
    while (off + 8 <= total) {
      uint32_t type = *(uint32_t *)(mb + off);
      uint32_t size = *(uint32_t *)(mb + off + 4);
      if (type == 3 && size >= 16) {
        uint32_t mod_start = *(uint32_t *)(mb + off + 8);
        uint32_t mod_end = *(uint32_t *)(mb + off + 12);
        if (mod_end > mod_start) {
          exec64_from_module((void *)(unsigned long long)mod_start,
                             (unsigned long long)(mod_end - mod_start));
        }
        break;
      }
      uint32_t adv = (size + 7) & ~7u;
      if (adv == 0)
        break;
      off += adv;
    }
  }

  for (;;) {
    asm volatile("hlt");
  }
}

extern "C" void isr64_handler(void) {
  volatile unsigned short *vga = (unsigned short *)0xB8000;
  const char *msg = "x86_64 exception";
  for (int i = 0; msg[i]; ++i) {
    vga[80 + i] = (unsigned short)((0x4F << 8) | msg[i]);
  }
  for (;;) {
    asm volatile("hlt");
  }
}
