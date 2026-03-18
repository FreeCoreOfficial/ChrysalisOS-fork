#include "../terminal.h"
#include "../time/timer.h"
#include "../memory/pmm.h"
#include "../string.h"
#include <stdint.h>

static void u64_to_dec(uint64_t v, char *buf, int buf_sz) {
  if (!buf || buf_sz <= 1) {
    if (buf && buf_sz > 0)
      buf[0] = 0;
    return;
  }
  if (v == 0) {
    buf[0] = '0';
    buf[1] = 0;
    return;
  }
  char tmp[32];
  int i = 0;
  while (v > 0 && i < (int)sizeof(tmp)) {
    tmp[i++] = '0' + (v % 10);
    v /= 10;
  }
  int j = 0;
  while (i > 0 && j < buf_sz - 1) {
    buf[j++] = tmp[--i];
  }
  buf[j] = 0;
}

extern "C" void cmd_sysinfo(const char *args) {
  (void)args;

  uint32_t uptime = timer_uptime_seconds();
  uint32_t total_frames = pmm_total_frames();
  uint32_t used_frames = pmm_used_frames();
  uint32_t free_frames = (total_frames > used_frames)
                             ? (total_frames - used_frames)
                             : 0;

  uint64_t total_bytes = (uint64_t)total_frames * (uint64_t)PAGE_SIZE;
  uint64_t free_bytes = (uint64_t)free_frames * (uint64_t)PAGE_SIZE;
  uint64_t used_bytes = (uint64_t)used_frames * (uint64_t)PAGE_SIZE;

  char buf[32];

  terminal_writestring("sysinfo:\n");

  terminal_writestring("  uptime: ");
  u64_to_dec(uptime, buf, sizeof(buf));
  terminal_writestring(buf);
  terminal_writestring(" s\n");

  terminal_writestring("  totalram: ");
  u64_to_dec(total_bytes / 1024, buf, sizeof(buf));
  terminal_writestring(buf);
  terminal_writestring(" KB\n");

  terminal_writestring("  freeram: ");
  u64_to_dec(free_bytes / 1024, buf, sizeof(buf));
  terminal_writestring(buf);
  terminal_writestring(" KB\n");

  terminal_writestring("  usedram: ");
  u64_to_dec(used_bytes / 1024, buf, sizeof(buf));
  terminal_writestring(buf);
  terminal_writestring(" KB\n");

  terminal_writestring("  mem_unit: 1\n");
}
