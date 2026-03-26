#include "exec64.h"
#include "../elf/elf64.h"
#include "../arch/x86_64/paging64.h"
#include "../arch/x86_64/user64.h"
#include "../string.h"
#include "../mem/kmalloc.h"
#include "../mem/user64_vm.h"
#include "../sched/task64.h"
#include "../drivers/serial.h"
#include "../fs/fs.h"
#ifndef __x86_64__
#include "../cmds/fat.h"
#endif

extern "C" void *kmalloc(size_t size);
extern "C" void kfree(void *ptr);
extern "C" const void *ramfs_read_file(const char *name, size_t *out_size);
#ifndef __x86_64__
extern "C" int fat32_read_file(const char *path, void *buf, uint32_t max_size);
extern "C" int32_t fat32_get_file_size(const char *path);
#endif

#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PT_PHDR 6
#define PT_TLS 7

#ifndef ET_DYN
#define ET_DYN 3
#endif

typedef struct {
  int64_t d_tag;
  union {
    uint64_t d_val;
    uint64_t d_ptr;
  } d_un;
} Elf64_Dyn;

typedef struct {
  uint32_t st_name;
  uint8_t st_info;
  uint8_t st_other;
  uint16_t st_shndx;
  uint64_t st_value;
  uint64_t st_size;
} Elf64_Sym;

typedef struct {
  uint64_t r_offset;
  uint64_t r_info;
  int64_t r_addend;
} Elf64_Rela;

#define DT_NULL 0
#define DT_NEEDED 1
#define DT_HASH 4
#define DT_STRTAB 5
#define DT_SYMTAB 6
#define DT_RELA 7
#define DT_RELASZ 8
#define DT_RELAENT 9
#define DT_STRSZ 10
#define DT_SYMENT 11
#define DT_PLTREL 20
#define DT_JMPREL 23
#define DT_PLTRELSZ 2
#define DT_GNU_HASH 0x6ffffef5

#define R_X86_64_64 1
#define R_X86_64_GLOB_DAT 6
#define R_X86_64_JUMP_SLOT 7
#define R_X86_64_RELATIVE 8

#define ELF64_R_SYM(i) ((uint32_t)((i) >> 32))
#define ELF64_R_TYPE(i) ((uint32_t)(i))
#define ELF64_ST_BIND(i) ((uint8_t)((i) >> 4))

#define STB_LOCAL 0
#define STB_GLOBAL 1
#define STB_WEAK 2

#define ELF64_DYN_BASE 0x0000000040000000ULL
#define ELF64_INTERP_BASE 0x0000000070000000ULL
#define ELF64_STACK_TOP 0x0000000080000000ULL
#define ELF64_STACK_SIZE (64 * 1024ULL)
#define ELF64_TLS_BASE 0x000000007ffe0000ULL

#define AT_NULL 0
#define AT_PHDR 3
#define AT_PHENT 4
#define AT_PHNUM 5
#define AT_PAGESZ 6
#define AT_BASE 7
#define AT_ENTRY 9
#define AT_UID 11
#define AT_EUID 12
#define AT_GID 13
#define AT_EGID 14
#define AT_HWCAP 16
#define AT_CLKTCK 17
#define AT_SECURE 23
#define AT_RANDOM 25
#define AT_EXECFN 31


typedef struct {
  const char *name;
  const uint8_t *file_data;
  size_t file_size;
  int owned;
  uint64_t load_base;
  uint64_t entry;
  uint64_t image_end;
  uint64_t phdr;
  uint64_t phent;
  uint64_t phnum;
  int is_main;

  uint64_t dyn_addr;
  uint64_t dyn_size;
  uint64_t symtab;
  uint64_t strtab;
  uint64_t strsz;
  uint64_t syment;
  uint64_t rela_addr;
  uint64_t rela_sz;
  uint64_t rela_ent;
  uint64_t jmprel_addr;
  uint64_t pltrel_sz;
  uint64_t pltrel_type;
  uint64_t hash_addr;
  uint64_t gnu_hash_addr;
  uint32_t sym_count;

  uint64_t tls_offset;
  uint64_t tls_filesz;
  uint64_t tls_memsz;
  uint64_t tls_align;
} elf64_image_t;

static const uint8_t *read_exec64(const char *path, size_t *out_size,
                                  int *out_owned) {
  if (out_owned)
    *out_owned = 0;

  if (path && path[0] == '/') {
    size_t ram_size = 0;
    const void *rdata = ramfs_read_file(path, &ram_size);
    if (rdata && ram_size > 0) {
      if (out_size)
        *out_size = ram_size;
      return (const uint8_t *)rdata;
    }
  }

#ifndef __x86_64__
  if (path && path[0] == '/') {
    fat_automount();
    int32_t fsz = fat32_get_file_size(path);
    if (fsz > 0) {
      uint8_t *buf = (uint8_t *)kmalloc((size_t)fsz);
      if (!buf)
        return nullptr;
      int bytes = fat32_read_file(path, buf, (uint32_t)fsz);
      if (bytes == fsz) {
        if (out_size)
          *out_size = (size_t)fsz;
        if (out_owned)
          *out_owned = 1;
        return buf;
      }
      kfree(buf);
    }
  }
#endif

  return nullptr;
}

static int read_exec64_path(const char *path, const uint8_t **out_data,
                            size_t *out_size, int *out_owned) {
  if (!out_data || !out_size)
    return -1;
  *out_data = read_exec64(path, out_size, out_owned);
  return (*out_data) ? 0 : -1;
}

static const char *env_lookup(char *const envp[], const char *key) {
  if (!envp || !key)
    return nullptr;
  size_t klen = strlen(key);
  for (int i = 0; envp[i]; i++) {
    const char *e = envp[i];
    if (!e)
      continue;
    if (strncmp(e, key, klen) == 0 && e[klen] == '=')
      return e + klen + 1;
  }
  return nullptr;
}

static int exec64_try_path(const char *dir, const char *name,
                           const uint8_t **out_data, size_t *out_size,
                           int *out_owned, char *out_path, size_t out_path_sz) {
  if (!dir || !*dir || !name || !*name || !out_data || !out_size)
    return -1;
  size_t dlen = strlen(dir);
  size_t nlen = strlen(name);
  size_t need = dlen + 1 + nlen + 1;
  if (need > out_path_sz)
    return -1;
  memcpy(out_path, dir, dlen);
  out_path[dlen] = '/';
  memcpy(out_path + dlen + 1, name, nlen);
  out_path[dlen + 1 + nlen] = 0;
  return read_exec64_path(out_path, out_data, out_size, out_owned);
}

static int map_user_segment(uint64_t vaddr, uint64_t memsz, uint64_t flags) {
  uint64_t start = vaddr & ~0xFFFULL;
  uint64_t end = (vaddr + memsz + 0xFFFULL) & ~0xFFFULL;
  for (uint64_t va = start; va < end; va += 0x1000) {
    uint64_t phys = paging64_alloc_frame();
    if (!phys)
      return -1;
    uint64_t map_flags = 0x1; /* P */
    if (flags & PF_W)
      map_flags |= 0x2; /* RW */
    map_flags |= 0x4; /* USER */
    if (paging64_map_page(va, phys, map_flags) < 0)
      return -1;
    /* Zero via the virtual address (not the physical frame address) so we
      don't corrupt kernel memory near the bump-allocator watermark. */
    memset((void *)(uintptr_t)va, 0, 0x1000);
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
    /* Zero via virtual address — same fix as map_user_segment. */
    memset((void *)(uintptr_t)va, 0, 0x1000);
  }
  return 0;
}

/* Removed all dynamic symbol and relocation logic. The kernel MUST NOT do this. */

static int load_elf64_image(const uint8_t *file_data, size_t file_size,
                            uint64_t load_base, elf64_image_t *out_img,
                            char *interp_buf, size_t interp_buf_sz) {
  if (!file_data || file_size < sizeof(elf64_ehdr_t)) return -1;
  const elf64_ehdr_t *eh = (const elf64_ehdr_t *)file_data;
  if (!elf64_is_valid(file_data, (uint32_t)file_size)) return -1;
  if (eh->e_machine != EM_X86_64 || (eh->e_type != ET_EXEC && eh->e_type != ET_DYN)) return -1;

  const elf64_phdr_t *phdr = (const elf64_phdr_t *)(file_data + eh->e_phoff);
  uint64_t image_end = 0;
  uint64_t phdr_addr = 0;

  for (uint16_t i = 0; i < eh->e_phnum; i++) {
    if (phdr[i].p_type == PT_PHDR) phdr_addr = load_base + phdr[i].p_vaddr;
    if (phdr[i].p_type == PT_INTERP && interp_buf && interp_buf_sz > 0) {
      size_t len = (size_t)phdr[i].p_filesz;
      if (len >= interp_buf_sz) len = interp_buf_sz - 1;
      memcpy(interp_buf, file_data + phdr[i].p_offset, len);
      interp_buf[len] = 0;
    }
    if (phdr[i].p_type == PT_TLS && out_img) {
      out_img->tls_offset = phdr[i].p_offset;
      out_img->tls_filesz = phdr[i].p_filesz;
      out_img->tls_memsz = phdr[i].p_memsz;
      out_img->tls_align = phdr[i].p_align;
    }
    if (phdr[i].p_type != PT_LOAD) continue;

    uint64_t seg_vaddr = load_base + phdr[i].p_vaddr;
    /* Map completely RW first to load the data safely and zero BSS */
    if (map_user_segment(seg_vaddr, phdr[i].p_memsz, PF_R | PF_W) < 0) return -1;

    if (phdr[i].p_filesz > 0) {
      memcpy((void *)(uintptr_t)seg_vaddr, file_data + phdr[i].p_offset, (size_t)phdr[i].p_filesz);
    }
    /* Zeros the BSS. map_user_segment zeroes entire pages, but we must zero the exact remainder just in case. */
    if (phdr[i].p_memsz > phdr[i].p_filesz) {
      memset((void *)(uintptr_t)(seg_vaddr + phdr[i].p_filesz), 0, (size_t)(phdr[i].p_memsz - phdr[i].p_filesz));
    }

    uint64_t end = seg_vaddr + phdr[i].p_memsz;
    if (end > image_end) image_end = end;
  }

  /* RE-ITERATE to apply strict permissions! (RX, RW, R) */
  for (uint16_t i = 0; i < eh->e_phnum; i++) {
    if (phdr[i].p_type != PT_LOAD) continue;
    uint64_t seg_vaddr = load_base + phdr[i].p_vaddr;
    uint64_t start = seg_vaddr & ~0xFFFULL;
    uint64_t end = (seg_vaddr + phdr[i].p_memsz + 0xFFFULL) & ~0xFFFULL;
    
    uint64_t map_flags = 0x5; /* P | USER */
    if (phdr[i].p_flags & PF_W) map_flags |= 0x2; /* RW */
    /* If the system supports NX, we would handle PF_X here via bit 63. Chrysalis paging64 doesn't yet, so we ignore PF_X. */
    
    for (uint64_t va = start; va < end; va += 0x1000) {
      paging64_protect_page(va, map_flags);
    }
  }

  if (out_img) {
    out_img->load_base = load_base;
    out_img->entry = load_base + eh->e_entry;
    out_img->image_end = image_end;
    out_img->phdr = phdr_addr ? phdr_addr : (load_base + eh->e_phoff);
    out_img->phent = eh->e_phentsize;
    out_img->phnum = eh->e_phnum;
  }
  return 0;
}

static uint64_t build_user_stack(char **argv, char **envp, uint64_t stack_top,
                                 uint64_t at_entry, uint64_t at_phdr,
                                 uint64_t at_phent, uint64_t at_phnum,
                                 uint64_t at_base) {
  uint64_t sp = stack_top;

  int argc = 0;
  while (argv && argv[argc]) argc++;
  int envc = 0;
  while (envp && envp[envc]) envc++;

  uint64_t arg_ptrs[64];
  uint64_t env_ptrs[64];

  for (int i = argc - 1; i >= 0; i--) {
    size_t len = strlen(argv[i]) + 1;
    sp -= (uint64_t)len;
    memcpy((void *)(uintptr_t)sp, argv[i], len);
    arg_ptrs[i] = sp;
  }

  for (int i = envc - 1; i >= 0; i--) {
    size_t len = strlen(envp[i]) + 1;
    sp -= (uint64_t)len;
    memcpy((void *)(uintptr_t)sp, envp[i], len);
    env_ptrs[i] = sp;
  }

  sp = sp & ~0xFULL; /* Initial alignment */

  int auxv_entries = 15; /* includes AT_NULL; add AT_EXECFN if argc>0 */
  if (argc > 0)
    auxv_entries++;

  uint64_t total_entries = 1 + (uint64_t)argc + 1 + (uint64_t)envc + 1 +
                           (uint64_t)auxv_entries * 2;
  uint64_t total_bytes = total_entries * 8;

  if (((sp - total_bytes) & 0xFULL) != 0) {
    sp -= 8; /* padding so final RSP is 16-byte aligned at entry */
  }

  auto push64 = [&](uint64_t v) {
    sp -= 8;
    *(uint64_t *)(uintptr_t)sp = v;
  };

  /* Pushing 16-byte random seed */
  for (int i = 0; i < 2; i++) {
    push64(0x1234567887654321ULL ^ (uint64_t)i);
  }
  uint64_t random_ptr = sp;

  /* Push AUX vector backwards */
  push64(0); push64(AT_NULL);
  
  if (argc > 0) {
    push64(arg_ptrs[0]); push64(AT_EXECFN);
  }

  push64(random_ptr); push64(AT_RANDOM);
  push64(0); push64(AT_SECURE);
  push64(0); push64(AT_EUID);
  push64(0); push64(AT_UID);
  push64(0); push64(AT_EGID);
  push64(0); push64(AT_GID);
  push64(0); push64(AT_HWCAP);
  push64(100); push64(AT_CLKTCK);

  push64(at_base);  push64(AT_BASE); /* interpreter base */
  push64(0x1000);   push64(AT_PAGESZ);
  push64(at_phnum); push64(AT_PHNUM);
  push64(at_phent); push64(AT_PHENT);
  push64(at_phdr);  push64(AT_PHDR);
  push64(at_entry); push64(AT_ENTRY); /* main executable entry */

  /* envp */
  push64(0);
  for (int i = envc - 1; i >= 0; i--) push64(env_ptrs[i]);

  /* argv */
  push64(0);
  for (int i = argc - 1; i >= 0; i--) push64(arg_ptrs[i]);

  push64((uint64_t)argc);

  return sp;
}

static bool is_canonical_addr(uint64_t addr) {
  uint64_t top = addr >> 48;
  return (top == 0) || (top == 0xFFFF);
}

static void dump_user_stack_layout(uint64_t rsp) {
  uint64_t *sp = (uint64_t *)(uintptr_t)rsp;
  uint64_t argc = sp[0];
  serial_write_string("[EXEC64] stack argc=");
  serial_printf("%u", (unsigned)argc);
  serial_write_string("\r\n");

  uint64_t *argv = sp + 1;
  for (uint64_t i = 0; i < argc; i++) {
    uint64_t ptr = argv[i];
    serial_write_string("[EXEC64] argv[");
    serial_printf("%u", (unsigned)i);
    serial_write_string("]=");
    serial_printf("%p", (void *)(uintptr_t)ptr);
    if (!is_canonical_addr(ptr)) {
      serial_write_string(" (NON-CANON)\r\n");
      return;
    }
    serial_write_string(" \"");
    const char *s = (const char *)(uintptr_t)ptr;
    for (int j = 0; j < 64 && s[j]; j++) {
      char c = s[j];
      if (c < 32 || c > 126) c = '.';
      serial_write(c);
    }
    serial_write_string("\"\r\n");
  }

  uint64_t *envp = argv + argc + 1;
  uint64_t envc = 0;
  while (envp[envc]) {
    uint64_t ptr = envp[envc];
    serial_write_string("[EXEC64] envp[");
    serial_printf("%u", (unsigned)envc);
    serial_write_string("]=");
    serial_printf("%p", (void *)(uintptr_t)ptr);
    if (!is_canonical_addr(ptr)) {
      serial_write_string(" (NON-CANON)\r\n");
      return;
    }
    serial_write_string(" \"");
    const char *s = (const char *)(uintptr_t)ptr;
    for (int j = 0; j < 64 && s[j]; j++) {
      char c = s[j];
      if (c < 32 || c > 126) c = '.';
      serial_write(c);
    }
    serial_write_string("\"\r\n");
    envc++;
    if (envc > 64) break;
  }

  uint64_t *auxv = envp + envc + 1;
  for (int i = 0; i < 32; i++) {
    uint64_t type = auxv[i * 2];
    uint64_t val = auxv[i * 2 + 1];
    serial_write_string("[EXEC64] auxv type=");
    serial_printf("%u", (unsigned)type);
    serial_write_string(" val=");
    serial_printf("%p", (void *)(uintptr_t)val);
    if ((type == AT_PHDR || type == AT_ENTRY || type == AT_BASE || type == AT_RANDOM || type == AT_EXECFN) &&
        !is_canonical_addr(val)) {
      serial_write_string(" (NON-CANON)");
    }
    serial_write_string("\r\n");
    if (type == AT_NULL) break;
  }
}

static const char *basename_dir(const char *path, char *buf, size_t buf_sz) {
  if (!buf || buf_sz == 0)
    return nullptr;
  buf[0] = 0;
  if (!path)
    return nullptr;
  const char *last = path;
  for (const char *p = path; *p; ++p) {
    if (*p == '/' || *p == '\\')
      last = p;
  }
  if (last == path)
    return nullptr;
  size_t len = (size_t)(last - path);
  if (len >= buf_sz)
    len = buf_sz - 1;
  memcpy(buf, path, len);
  buf[len] = 0;
  return buf;
}

static int build_lib_path(const char *prefix, const char *name, char *out,
                          size_t out_sz) {
  if (!prefix || !name || !out || out_sz == 0)
    return -1;
  size_t plen = strlen(prefix);
  size_t nlen = strlen(name);
  size_t need = plen + 1 + nlen + 1;
  if (need > out_sz)
    return -1;
  memcpy(out, prefix, plen);
  out[plen] = '/';
  memcpy(out + plen + 1, name, nlen);
  out[plen + 1 + nlen] = 0;
  return 0;
}

static int elf64_collect_needed(const elf64_image_t *img, const char **out,
                                int max) {
  if (!img || !out || max <= 0 || !img->dyn_addr || !img->strtab)
    return 0;
  int count = 0;
  const Elf64_Dyn *dyn = (const Elf64_Dyn *)(uintptr_t)img->dyn_addr;
  uint64_t dyn_count = img->dyn_size / sizeof(Elf64_Dyn);
  for (uint64_t i = 0; i < dyn_count; i++) {
    uint64_t tag = (uint64_t)dyn[i].d_tag;
    if (tag == DT_NULL)
      break;
    if (tag != DT_NEEDED)
      continue;
    uint64_t off = dyn[i].d_un.d_val;
    if (off >= img->strsz)
      continue;
    const char *name = (const char *)(uintptr_t)(img->strtab + off);
    if (!name || !*name)
      continue;
    if (count < max)
      out[count++] = name;
  }
  return count;
}

static int elf64_is_loaded(elf64_image_t *images, int img_count,
                           const char *name) {
  if (!name)
    return 1;
  for (int i = 0; i < img_count; i++) {
    if (images[i].name && strcmp(images[i].name, name) == 0)
      return 1;
  }
  return 0;
}

static int elf64_find_library(const char *name, const char *base_dir,
                              const uint8_t **out_data, size_t *out_size,
                              int *out_owned) {
  if (!name || !out_data || !out_size)
    return -1;

  if (strchr(name, '/'))
    return read_exec64_path(name, out_data, out_size, out_owned);

  const char *prefixes[] = {base_dir, "/system/lib", "/lib", "/lib64", 
                            "/lib/x86_64-linux-gnu", "/usr/lib", nullptr};

  char path[256];
  for (int i = 0; i < (int)(sizeof(prefixes) / sizeof(prefixes[0])); i++) {
    const char *prefix = prefixes[i];
    if (!prefix || !prefix[0])
      continue;
    if (build_lib_path(prefix, name, path, sizeof(path)) < 0)
      continue;
    
    serial_write_string("[K64]   trying library path: ");
    serial_write_string(path);
    serial_write_string("\r\n");

    if (read_exec64_path(path, out_data, out_size, out_owned) == 0)
      return 0;
  }
  return -1;


}

static char **copy_string_array(char *const user_arr[]) {
  if (!user_arr) return nullptr;
  int count = 0;
  while (user_arr[count]) count++;
  char **karr = (char **)kmalloc((size_t)(count + 1) * sizeof(char *));
  if (!karr) return nullptr;
  for (int i = 0; i < count; i++) {
    size_t len = strlen(user_arr[i]) + 1;
    karr[i] = (char *)kmalloc(len);
    if (karr[i]) memcpy(karr[i], user_arr[i], len);
  }
  karr[count] = nullptr;
  return karr;
}

static void free_string_array(char **karr) {
  if (!karr) return;
  for (int i = 0; karr[i]; i++) kfree(karr[i]);
  kfree(karr);
}

static int exec64_from_buffer(const uint8_t *file_data, size_t file_size,
                              char *const argv[], char *const envp[], const char *image_path) {
  task64_t *t = task64_current();
  if (!t) return -1;

  serial_write_string("[EXEC64] exec64_from_buffer enter\r\n");

  char **kargv = copy_string_array(argv);
  char **kenvp = copy_string_array(envp);

  /* Copy file data to kernel heap so it remains accessible after PML4 switch */
  uint8_t *kfile = (uint8_t *)kmalloc(file_size);
  if (!kfile) {
    free_string_array(kargv);
    free_string_array(kenvp);
    return -1;
  }
  memcpy(kfile, file_data, file_size);

  serial_write_string("[EXEC64] data copied to kernel heap\r\n");

  /* Allocate new PML4 while still running under boot/kernel PML4 */
  uint64_t new_cr3 = paging64_new_user_pml4();
  if (!new_cr3) {
    kfree(kfile);
    free_string_array(kargv);
    free_string_array(kenvp);
    return -1;
  }

  serial_write_string("[EXEC64] new PML4 allocated\r\n");

  /* Switch g_pml4 AND CR3 to the new PML4 so that paging64_map_page
   * will create user mappings in the new address space */
  paging64_set_active_pml4(new_cr3);
  t->cr3 = new_cr3;

  serial_write_string("[EXEC64] switched to new PML4\r\n");

  elf64_image_t images[2];
  int img_count = 0;
  char interp_path[256];
  interp_path[0] = 0;

  const elf64_ehdr_t *eh = (const elf64_ehdr_t *)kfile;
  uint64_t main_base = (eh && eh->e_type == ET_DYN) ? ELF64_DYN_BASE : 0;

  serial_write_string("[EXEC64] loading main ELF\r\n");

  if (load_elf64_image(kfile, file_size, main_base, &images[0], interp_path, sizeof(interp_path)) < 0) {
    serial_write_string("[EXEC64] load_elf64_image FAILED\r\n");
    kfree(kfile);
    paging64_restore_boot_pml4();
    return -1;
  }
  img_count = 1;
  serial_write_string("[EXEC64] main image loaded\r\n");

  int interp_index = -1;
  if (interp_path[0]) {
    serial_write_string("[EXEC64] loading interpreter: ");
    serial_write_string(interp_path);
    serial_write_string("\r\n");

    /* read_exec64_path returns ramfs pointers — still accessible since
     * we preserved kernel identity mappings in the new PML4 */
    const uint8_t *interp_data = nullptr;
    size_t interp_size = 0;
    int interp_owned = 0;
    if (read_exec64_path(interp_path, &interp_data, &interp_size, &interp_owned) < 0) {
      serial_write_string("[EXEC64] interpreter read FAILED\r\n");
      kfree(kfile);
      paging64_restore_boot_pml4();
      return -1;
    }

    if (load_elf64_image(interp_data, interp_size, ELF64_INTERP_BASE, &images[img_count], nullptr, 0) < 0) {
      serial_write_string("[EXEC64] interpreter load FAILED\r\n");
      kfree(kfile);
      paging64_restore_boot_pml4();
      return -1;
    }
    images[img_count].owned = interp_owned;
    interp_index = img_count;
    img_count++;
    serial_write_string("[EXEC64] interpreter loaded\r\n");
  }

  kfree(kfile);

  uint64_t image_end = 0;
  for (int i = 0; i < img_count; i++) {
    if (images[i].image_end > image_end) image_end = images[i].image_end;
  }

  uint64_t stack_top = ELF64_STACK_TOP;
  if (map_user_stack(stack_top, ELF64_STACK_SIZE) < 0) {
    paging64_restore_boot_pml4();
    return -1;
  }
  serial_write_string("[EXEC64] stack mapped\r\n");

  uint64_t at_base = (interp_index >= 0) ? images[interp_index].load_base : 0;
  uint64_t entry = (interp_index >= 0) ? images[interp_index].entry : images[0].entry;
  
  uint64_t rsp = build_user_stack(kargv, kenvp, stack_top,
                                  images[0].entry, images[0].phdr,
                                  images[0].phent, images[0].phnum, at_base);

  serial_write_string("[EXEC64] stack built: rsp=");
  serial_printf("0x%x", rsp);
  serial_write_string(" align=");
  serial_printf("%u", (unsigned)(rsp & 0xF));
  serial_write_string(" entry=");
  serial_printf("0x%x", entry);
  serial_write_string("\r\n");

  dump_user_stack_layout(rsp);

  free_string_array(kargv);
  free_string_array(kenvp);

  user64_init_process(image_end);
  if (image_path) {
    strncpy(t->exe_path, image_path, sizeof(t->exe_path) - 1);
    t->exe_path[sizeof(t->exe_path) - 1] = 0;
  }
  task64_set_user_stack(rsp);

  serial_write_string("[EXEC64] entering user mode: rip=");
  serial_printf("%p", (void *)entry);
  serial_write_string(" rsp=");
  serial_printf("%p", (void *)rsp);
  serial_write_string("\r\n");

  user64_enter(entry, rsp, 0x202, 0, 0);
  return 0;
}

int exec64_from_module(void *start, uint64_t size, const char *image_path) {
  char *argv[] = {(char *)"module", nullptr};
  char *envp[] = {nullptr};
  return exec64_from_buffer((const uint8_t *)start, (size_t)size, argv, envp, image_path);
}

int execve_linux_x86_64_full(const char *filename, char *const argv[], char *const envp[]) {
  if (!filename || !*filename)
    return -2;

  size_t file_size = 0;
  int owned = 0;
  const uint8_t *file_data = nullptr;
  char chosen_path[256];
  chosen_path[0] = 0;

  const char *path_env = env_lookup(envp, "PATH");
  char *fallback_envp[3];
  if (!path_env || !*path_env) {
    fallback_envp[0] = (char *)"PATH=/usr/bin:/usr/lib/xorg:/bin:/usr/lib";
    fallback_envp[1] = (char *)"HOME=/root";
    fallback_envp[2] = nullptr;
    envp = fallback_envp;
    path_env = fallback_envp[0] + 5;
  }

  if (strchr(filename, '/')) {
    file_data = read_exec64(filename, &file_size, &owned);
    if (!file_data)
      return -2;
    int r = exec64_from_buffer(file_data, file_size, argv, envp, filename);
    if (owned) kfree((void *)file_data);
    return r;
  }

  if (path_env && *path_env) {
    const char *p = path_env;
    while (*p) {
      const char *start = p;
      while (*p && *p != ':')
        p++;
      size_t len = (size_t)(p - start);
      if (len > 0 && len < sizeof(chosen_path)) {
        char dir[256];
        memcpy(dir, start, len);
        dir[len] = 0;
        if (exec64_try_path(dir, filename, &file_data, &file_size, &owned,
                            chosen_path, sizeof(chosen_path)) == 0) {
          int r = exec64_from_buffer(file_data, file_size, argv, envp, chosen_path);
          if (owned) kfree((void *)file_data);
          return r;
        }
      }
      if (*p == ':')
        p++;
    }
  }

  const char *fallbacks[] = {"/usr/bin", "/usr/lib/xorg", "/bin", "/usr/lib", nullptr};
  for (int i = 0; fallbacks[i]; i++) {
    if (exec64_try_path(fallbacks[i], filename, &file_data, &file_size, &owned,
                        chosen_path, sizeof(chosen_path)) == 0) {
      int r = exec64_from_buffer(file_data, file_size, argv, envp, chosen_path);
      if (owned) kfree((void *)file_data);
      return r;
    }
  }

  if (strcmp(filename, "X") == 0) {
    const char *xalts[] = {"/usr/lib/xorg/Xorg", "/usr/bin/Xorg", nullptr};
    for (int i = 0; xalts[i]; i++) {
      if (read_exec64_path(xalts[i], &file_data, &file_size, &owned) == 0) {
        int r = exec64_from_buffer(file_data, file_size, argv, envp, xalts[i]);
        if (owned) kfree((void *)file_data);
        return r;
      }
    }
  }

  return -2;
}
