/* kernel/proc/exec.cpp
 * ELF Loader and Execution for ChrysalisOS
 */

#include "exec.h"
#include "../cmds/cs.h"
#include "../cmds/fat.h"
#include "../fs/chrysfs/chrysfs.h"
#include "../fs/fs.h"
#include "../include/task.h"
#include "../mem/kmalloc.h"
#include "../mm/address_space.h"
#include "../memory/pmm.h"
#include "../mm/paging.h"
#include "../mm/vmm.h"
#include "../string.h"
#include "../terminal.h"

/* FAT32 Driver API */
extern "C" int fat32_read_file(const char *path, void *buf, uint32_t max_size);
extern "C" void serial(const char *fmt, ...);

/* ELF Header Definitions */
#define ELF_MAGIC 0x464C457F

typedef uint32_t Elf32_Addr;
typedef uint32_t Elf32_Off;
typedef uint16_t Elf32_Half;
typedef uint32_t Elf32_Word;

typedef struct {
  uint8_t e_ident[16];
  Elf32_Half e_type;
  Elf32_Half e_machine;
  Elf32_Word e_version;
  Elf32_Addr e_entry;
  Elf32_Off e_phoff;
  Elf32_Off e_shoff;
  Elf32_Word e_flags;
  Elf32_Half e_ehsize;
  Elf32_Half e_phentsize;
  Elf32_Half e_phnum;
  Elf32_Half e_shentsize;
  Elf32_Half e_shnum;
  Elf32_Half e_shstrndx;
} Elf32_Ehdr;

typedef struct {
  Elf32_Word p_type;
  Elf32_Off p_offset;
  Elf32_Addr p_vaddr;
  Elf32_Addr p_paddr;
  Elf32_Word p_filesz;
  Elf32_Word p_memsz;
  Elf32_Word p_flags;
  Elf32_Word p_align;
} Elf32_Phdr;

#define PT_LOAD 1

/* Helper to read file content into a buffer */
static uint8_t *read_executable(const char *path, size_t *out_size) {
  /* 1. Try Disk (FAT32) */
  if (path[0] == '/') {
    fat_automount();

    /* Allocate a reasonable buffer for the binary */
    size_t max_size = 1024 * 1024; // 1MB limit for now
    uint8_t *buf = (uint8_t *)kmalloc(max_size);
    if (!buf)
      return nullptr;

    int bytes = fat32_read_file(path, buf, max_size);
    if (bytes > 0) {
      *out_size = (size_t)bytes;
      return buf;
    }
    kfree(buf);
  }

  /* 2. Try RAMFS */
  const FSNode *node = fs_find(path);
  if (node && node->data) {
    const char *text = (const char *)node->data;
    size_t len = strlen(text);
    uint8_t *buf = (uint8_t *)kmalloc(len);
    if (!buf)
      return nullptr;
    memcpy(buf, text, len);
    *out_size = len;
    return buf;
  }

  return nullptr;
}

extern "C" int execve(const char *filename, char *const argv[],
                      char *const envp[]) {
  (void)envp;

  /* Hook for Chrysalis Script Interpreter (/bin/cs) */
  if (strcmp(filename, "/bin/cs") == 0) {
    int argc = 0;
    while (argv && argv[argc])
      argc++;
    return cmd_cs_main(argc, (char **)argv);
  }

  serial("[EXEC] Loading '%s'...\n", filename);
  terminal_printf("[EXEC] Loading '%s'...\n", filename);

  size_t file_size = 0;
  uint8_t *file_data = read_executable(filename, &file_size);

  if (!file_data) {
    serial("[EXEC] Error: Could not read file '%s'\n", filename);
    terminal_printf("[EXEC] Error: Could not read file '%s'\n", filename);
    return -1;
  }

  /* Parse ELF Header */
  if (file_size < sizeof(Elf32_Ehdr)) {
    terminal_printf("[EXEC] Error: File too small for ELF header\n");
    kfree(file_data);
    return -1;
  }

  Elf32_Ehdr *ehdr = (Elf32_Ehdr *)file_data;
  if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' ||
      ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F') {
    terminal_printf("[EXEC] Error: Invalid ELF magic\n");
    kfree(file_data);
    return -1;
  }

  terminal_printf("[EXEC] ELF Loaded. Entry=0x%x, Segments=%d\n", ehdr->e_entry,
                  ehdr->e_phnum);

  /* Create a private address space for this user app so multiple .petal apps
     can coexist without overlapping at the same virtual addresses. */
  address_space_t *as = address_space_create();
  if (!as || !as->page_directory) {
    terminal_printf("[EXEC] Error: Could not create address space\n");
    kfree(file_data);
    return -1;
  }

  /* Load Segments */
  Elf32_Phdr *phdr = (Elf32_Phdr *)(file_data + ehdr->e_phoff);
  for (int i = 0; i < ehdr->e_phnum; i++) {
    if (phdr[i].p_type == PT_LOAD) {
      if ((size_t)phdr[i].p_offset + (size_t)phdr[i].p_filesz > file_size) {
        terminal_printf("[EXEC] Error: Segment %d out of bounds\n", i);
        address_space_destroy(as);
        kfree(file_data);
        return -1;
      }

      uint32_t start_page = phdr[i].p_vaddr & PAGE_FRAME_MASK;
      uint32_t end_page =
          (phdr[i].p_vaddr + phdr[i].p_memsz + PAGE_SIZE - 1) & PAGE_FRAME_MASK;

      /* Allocate pages */
      for (uint32_t page = start_page; page < end_page; page += PAGE_SIZE) {
        uint32_t *pte = get_pte_for(as->page_directory, page, 0);
        if (!pte || !(*pte & PAGE_PRESENT)) {
          void *new_page = vmm_alloc_page();
          if (!new_page) {
            terminal_printf("[EXEC] Error: OOM during load\n");
            address_space_destroy(as);
            kfree(file_data);
            return -1;
          }
          uint32_t phys = vmm_virt_to_phys(new_page);
          if (!phys) {
            terminal_printf("[EXEC] Error: Invalid page mapping\n");
            address_space_destroy(as);
            kfree(file_data);
            return -1;
          }
          vmm_map_page(as->page_directory, page, phys,
                       PAGE_PRESENT | PAGE_RW | PAGE_USER);
          memset(new_page, 0, PAGE_SIZE);
        }
      }

      /* Copy file-backed bytes into mapped physical pages (BSS remains zeroed).
       */
      for (uint32_t off = 0; off < phdr[i].p_filesz; off++) {
        uint32_t va = phdr[i].p_vaddr + off;
        uint32_t page = va & PAGE_FRAME_MASK;
        uint32_t in_page = va & (PAGE_SIZE - 1);

        uint32_t *pte = get_pte_for(as->page_directory, page, 0);
        if (!pte || !(*pte & PAGE_PRESENT)) {
          terminal_printf("[EXEC] Error: Missing PTE while copying segment %d\n",
                          i);
          address_space_destroy(as);
          kfree(file_data);
          return -1;
        }

        uint32_t phys_page = *pte & PAGE_FRAME_MASK;
        uint8_t *dst =
            (uint8_t *)(uintptr_t)(phys_page + KERNEL_BASE + in_page);
        *dst = file_data[phdr[i].p_offset + off];
      }
    }
  }

  uint32_t as_cr3 = vmm_virt_to_phys(as->page_directory);
  if (!as_cr3) {
    terminal_printf("[EXEC] Error: Could not resolve address-space CR3\n");
    address_space_destroy(as);
    kfree(file_data);
    return -1;
  }

  /* Cleanup buffer */
  void (*entry_point)(void) = (void (*)(void))ehdr->e_entry;
  kfree(file_data);

  /* Create background task */
  serial("[EXEC] Spawning task for %s at 0x%x\n", filename, entry_point);
  task_t *t = task_create(entry_point, 0);
  if (t) {
    t->is_user_app = 1;
    t->cr3 = as_cr3;
    t->launch_arg[0] = 0;
    if (argv && argv[1]) {
      strncpy(t->launch_arg, argv[1], sizeof(t->launch_arg) - 1);
      t->launch_arg[sizeof(t->launch_arg) - 1] = 0;
    }
  } else {
    terminal_printf("[EXEC] Error: Could not create task\n");
    address_space_destroy(as);
    return -1;
  }

  return 0;
}

extern "C" int exec_from_path(const char *path, char *const argv[]) {
  return execve(path, argv, nullptr);
}
