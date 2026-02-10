#include "sysfetch.h"
#include "../drivers/pit.h"
#include "../hardware/hpet.h"
#include "../memory/pmm.h"
#include "../terminal.h"
#include "../user/user.h"
#include "../video/gpu.h"

/* Fallbacks if Makefile doesn't provide them */
#ifndef KERNEL_CODENAME
#define KERNEL_CODENAME "mosquito-with-violin"
#endif

#ifndef CHRYVER
#define CHRYVER "chrysver-0.2.1"
#endif

/* Helper to get CPU Brand String using CPUID */
static void get_cpu_model(char *buf) {
  uint32_t eax, ebx, ecx, edx;
  uint32_t *ptr = (uint32_t *)buf;

  /* Check if extended functions are supported */
  asm volatile("cpuid" : "=a"(eax) : "a"(0x80000000) : "ebx", "ecx", "edx");
  if (eax < 0x80000004) {
    /* Fallback if brand string not supported */
    for (int i = 0; i < 12; i++)
      buf[i] = "Generic x86"[i];
    buf[11] = 0;
    return;
  }

  /* Brand string is in 0x80000002, 0x80000003, 0x80000004 */
  for (uint32_t i = 0; i < 3; i++) {
    asm volatile("cpuid"
                 : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                 : "a"(0x80000002 + i));
    ptr[i * 4 + 0] = eax;
    ptr[i * 4 + 1] = ebx;
    ptr[i * 4 + 2] = ecx;
    ptr[i * 4 + 3] = edx;
  }
  buf[48] = 0;

  /* Trim leading spaces */
  char *start = buf;
  while (*start == ' ')
    start++;
  if (start != buf) {
    int j = 0;
    while (start[j]) {
      buf[j] = start[j];
      j++;
    }
    buf[j] = 0;
  }
}

extern "C" void cmd_sysfetch(const char *args) {
  (void)args;

  user_t *u = user_get_current();
  const char *user_name = u ? u->name : "guest";

  uint32_t total_mb = (pmm_total_frames() * 4096) / (1024 * 1024);
  uint32_t used_mb = (pmm_used_frames() * 4096) / (1024 * 1024);

  /* Calculate uptime using HPET if available, else PIT fallback */
  uint32_t uptime_sec = 0;
  if (hpet_is_active()) {
    uptime_sec = (uint32_t)(hpet_time_ms() / 1000);
  } else {
    uptime_sec = (uint32_t)(pit_get_ticks() / 100);
  }

  char cpu_model[64];
  get_cpu_model(cpu_model);

  gpu_device_t *gpu = gpu_get_primary();
  const char *gpu_name = "VGA Standard";
  int res_w = 80, res_h = 25;

  if (gpu) {
    if (gpu->type == GPU_TYPE_BOCHS)
      gpu_name = "Bochs VBE";
    else if (gpu->type == GPU_TYPE_VMWARE)
      gpu_name = "VMware SVGA";
    else if (gpu->type == GPU_TYPE_QEMU)
      gpu_name = "QEMU CIRRUS";
    else if (gpu->type == GPU_TYPE_VESA)
      gpu_name = "VESA VBE";

    res_w = gpu->width;
    res_h = gpu->height;
  }

  /* ASCII Art Butterfly - Improved */
  terminal_writestring("\n");

  // Line 1
  terminal_set_text_attr(0x0D); // Light Magenta
  terminal_writestring("  _      _   ");
  terminal_set_text_attr(0x0A); // Light Green
  terminal_writestring(user_name);
  terminal_set_text_attr(0x0F); // White
  terminal_writestring("@");
  terminal_set_text_attr(0x0A);
  terminal_writestring("chrysalis\n");

  // Line 2
  terminal_set_text_attr(0x0D);
  terminal_writestring(" { `\\  /` }  ");
  terminal_set_text_attr(0x07); // Light Gray
  terminal_writestring("---------------\n");

  // Line 3
  terminal_set_text_attr(0x0D);
  terminal_writestring(" {   \\/   }  ");
  terminal_set_text_attr(0x0B); // Light Cyan
  terminal_writestring("OS: ");
  terminal_set_text_attr(0x0F);
  terminal_printf("Chrysalis OS %s\n", CHRYVER);

  // Line 4
  terminal_set_text_attr(0x0D);
  terminal_writestring(" {   /\\   }  ");
  terminal_set_text_attr(0x0B);
  terminal_writestring("Kernel: ");
  terminal_set_text_attr(0x0F);
  terminal_printf("%s\n", KERNEL_CODENAME);

  // Line 5
  terminal_set_text_attr(0x0D);
  terminal_writestring(" {_,/  \\._}  ");
  terminal_set_text_attr(0x0B);
  terminal_writestring("Uptime: ");
  terminal_set_text_attr(0x0F);
  terminal_printf("%d min, %d sec\n", uptime_sec / 60, uptime_sec % 60);

  // Line 6
  terminal_set_text_attr(0x0D);
  terminal_writestring("             ");
  terminal_set_text_attr(0x0B);
  terminal_writestring("Resolution: ");
  terminal_set_text_attr(0x0F);
  terminal_printf("%dx%d\n", res_w, res_h);

  // Line 7
  terminal_set_text_attr(0x0D);
  terminal_writestring("             ");
  terminal_set_text_attr(0x0B);
  terminal_writestring("CPU: ");
  terminal_set_text_attr(0x0F);
  terminal_writestring(cpu_model);
  terminal_writestring("\n");

  // Line 8
  terminal_set_text_attr(0x0D);
  terminal_writestring("             ");
  terminal_set_text_attr(0x0B);
  terminal_writestring("GPU: ");
  terminal_set_text_attr(0x0F);
  terminal_writestring(gpu_name);
  terminal_writestring("\n");

  // Line 9
  terminal_set_text_attr(0x0D);
  terminal_writestring("             ");
  terminal_set_text_attr(0x0B);
  terminal_writestring("Memory: ");
  terminal_set_text_attr(0x0F);
  terminal_printf("%d MB / %d MB\n", used_mb, total_mb);

  // Line 10 (Shell)
  terminal_set_text_attr(0x0D);
  terminal_writestring("             ");
  terminal_set_text_attr(0x0B);
  terminal_writestring("Shell: ");
  terminal_set_text_attr(0x0F);
  terminal_writestring("CSH (Chrysalis Shell)\n");

  // Line 11 (Color Palette)
  terminal_writestring("\n             ");
  for (int i = 0; i < 8; i++) {
    terminal_set_text_attr((uint8_t)(i << 4) | (uint8_t)i);
    terminal_writestring("  ");
  }
  terminal_set_text_attr(0x0F);
  terminal_writestring("\n");
}
