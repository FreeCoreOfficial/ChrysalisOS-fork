#include "exec64.h"
#include "../elf/elf64.h"
#include "../arch/x86_64/paging64.h"
#include "../arch/x86_64/user64.h"
#include "../string.h"
#include "../mem/kmalloc.h"
extern "C" void *kmalloc(size_t size);
extern "C" void kfree(void *ptr);

static int map_user_segment(uint64_t vaddr, uint64_t memsz, uint64_t flags) {
  uint64_t start = vaddr & ~0xFFFULL;
  uint64_t end = (vaddr + memsz + 0xFFFULL) & ~0xFFFULL;
  for (uint64_t va = start; va < end; va += 0x1000) {
    uint64_t phys = paging64_alloc_frame();
    if (!phys)
      return -1;
    uint64_t map_flags = 0x3; /* P|RW */
    if (flags & PF_X)
      map_flags |= 0; /* NX not implemented */
    map_flags |= 0x4; /* USER */
    if (paging64_map_page(va, phys, map_flags) < 0)
      return -1;
    memset((void *)(uint64_t)phys, 0, 0x1000);
  }
  return 0;
}

static int map_user_stack(uint64_t top, uint64_t bytes) {
  uint64_t start = (top - bytes) & ~0xFFFULL;
  for (uint64_t va = start; va < top; va += 0x1000) {
    uint64_t phys = paging64_alloc_frame();
    if (!phys)
      return -1;
    if (paging64_map_page(va, phys, 0x7) < 0)
      return -1;
    memset((void *)(uint64_t)phys, 0, 0x1000);
  }
  return 0;
}

static int exec64_from_buffer(const uint8_t *file_data, size_t file_size) {
  if (!file_data || file_size == 0)
    return -1;

  elf64_load_info_t info;
  int r = elf64_load_from_buffer((void *)file_data, (uint32_t)file_size, &info);
  if (r < 0) {
    return -1;
  }

  for (int i = 0; i < info.seg_count; ++i) {
    elf64_segment_t *s = &info.segments[i];
    if (map_user_segment(s->vaddr, s->memsz, s->flags) < 0) {
      elf64_unload_kernel_space(&info);
      return -1;
    }
    memcpy((void *)(uint64_t)s->vaddr, s->kernel_buf, (uint32_t)s->filesz);
  }

  uint64_t stack_top = 0x0000000080000000ULL;
  if (map_user_stack(stack_top, 0x4000) < 0) {
    elf64_unload_kernel_space(&info);
    return -1;
  }

  struct user64_context ctx;
  ctx.rip = info.entry_point;
  ctx.rsp = stack_top;
  ctx.rflags = 0x202;

  elf64_unload_kernel_space(&info);
  user64_enter(&ctx);
  return 0;
}

int exec64_from_module(void *start, uint64_t size) {
  return exec64_from_buffer((const uint8_t *)start, (size_t)size);
}

int execve_linux_x86_64_full(const char *filename, char *const argv[]) {
  (void)filename;
  (void)argv;
  return -1;
}
