#include "exec64.h"
#include "../elf/elf64.h"
#include "../arch/x86_64/paging64.h"
#include "../arch/x86_64/syscall64.h"
#include "../arch/x86_64/user64.h"
#include "../string.h"
#include "../mem/kmalloc.h"
#include "../mem/user64_vm.h"
#include "../sched/task64.h"
#include "../drivers/serial.h"
#include "../terminal.h"
#include "../fs/fs.h"
#include <stdarg.h>
#ifndef __x86_64__
#include "../cmds/fat.h"
#endif

extern "C" void *kmalloc(size_t size);
extern "C" void kfree(void *ptr);
extern "C" const void *ramfs_read_file(const char *name, size_t *out_size);
extern "C" void serial_vprintf(const char *fmt, va_list args);
#ifndef __x86_64__
extern "C" int fat32_read_file(const char *path, void *buf, uint32_t max_size);
extern "C" int32_t fat32_get_file_size(const char *path);
#endif

extern "C" void terminal_vprintf(const char *fmt, void *va_ptr) {
  if (!va_ptr)
    return;
  va_list args;
  va_copy(args, *(va_list *)va_ptr);
  serial_vprintf(fmt, args);
  va_end(args);
}

extern "C" void terminal_printf(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  serial_vprintf(fmt, args);
  va_end(args);
}

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
#define DT_REL 17
#define DT_PLTREL 20
#define DT_JMPREL 23
#define DT_PLTRELSZ 2
#define DT_RELRSZ 35
#define DT_RELR 36
#define DT_RELRENT 37
#define DT_GNU_HASH 0x6ffffef5

#define R_X86_64_NONE 0
#define R_X86_64_64 1
#define R_X86_64_COPY 5
#define R_X86_64_GLOB_DAT 6
#define R_X86_64_JUMP_SLOT 7
#define R_X86_64_RELATIVE 8
#define R_X86_64_DTPMOD64 16
#define R_X86_64_DTPOFF64 17
#define R_X86_64_TPOFF64 18
#define R_X86_64_IRELATIVE 37

#define ELF64_R_SYM(i) ((uint32_t)((i) >> 32))
#define ELF64_R_TYPE(i) ((uint32_t)(i))
#define ELF64_ST_BIND(i) ((uint8_t)((i) >> 4))
#define ELF64_ST_TYPE(i) ((uint8_t)((i) & 0xF))

#define STB_LOCAL 0
#define STB_GLOBAL 1
#define STB_WEAK 2
#define STT_GNU_IFUNC 10

#ifndef SHN_UNDEF
#define SHN_UNDEF 0
#endif

#define ELF64_DYN_BASE 0x0000000040000000ULL
#define ELF64_INTERP_BASE 0x0000000070000000ULL
#define ELF64_LIB_BASE 0x0000000060000000ULL
#define ELF64_LIB_STRIDE 0x02000000ULL
#define ELF64_STACK_TOP 0x0000000080000000ULL
#define ELF64_STACK_SIZE (64 * 1024ULL)
#define ELF64_TLS_BASE 0x000000007ffe0000ULL

#define USER64_PROT_READ 0x1
#define USER64_PROT_WRITE 0x2

#define AT_NULL 0
#define AT_PHDR 3
#define AT_PHENT 4
#define AT_PHNUM 5
#define AT_PAGESZ 6
#define AT_BASE 7
#define AT_FLAGS 8
#define AT_ENTRY 9
#define AT_UID 11
#define AT_EUID 12
#define AT_GID 13
#define AT_EGID 14
#define AT_HWCAP 16
#define AT_CLKTCK 17
#define AT_SECURE 23
#define AT_RANDOM 25
#define AT_HWCAP2 26
#define AT_EXECFN 31

static constexpr bool k_exec64_kernel_link_dynamic = true;


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
  uint64_t relr_addr;
  uint64_t relr_sz;
  uint64_t relr_ent;
  uint64_t hash_addr;
  uint64_t gnu_hash_addr;
  uint32_t sym_count;
  int needed_count;

  uint64_t tls_offset;
  uint64_t tls_filesz;
  uint64_t tls_memsz;
  uint64_t tls_align;
  uint64_t tls_block_offset;
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

static void exec64_abort(const char *msg) {
  terminal_printf("[EXEC64] %s\n", msg ? msg : "fatal error");
  for (;;) {
  }
}

static void exec64_abortf(const char *fmt, ...) {
  va_list args;
  terminal_printf("[EXEC64] ");
  va_start(args, fmt);
  terminal_vprintf(fmt, &args);
  va_end(args);
  terminal_printf("\n");
  for (;;) {
  }
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

static int phdr_flags_to_user_prot(uint32_t flags) {
  int prot = USER64_PROT_READ;
  if (flags & PF_W)
    prot |= USER64_PROT_WRITE;
  return prot;
}

static uint32_t elf64_sym_count_from_hash(uint64_t hash_addr) {
  if (!hash_addr)
    return 0;
  uint32_t *hash = (uint32_t *)(uintptr_t)hash_addr;
  return hash[1];
}

static uint32_t elf64_sym_count_from_gnu_hash(uint64_t gnu_hash_addr) {
  if (!gnu_hash_addr)
    return 0;
  uint32_t *hdr = (uint32_t *)(uintptr_t)gnu_hash_addr;
  uint32_t nbuckets = hdr[0];
  uint32_t symoffset = hdr[1];
  uint32_t bloom_size = hdr[2];
  if (nbuckets == 0)
    return 0;

  uint64_t *bloom = (uint64_t *)(uintptr_t)(gnu_hash_addr + 16);
  (void)bloom;
  uint32_t *buckets =
      (uint32_t *)(uintptr_t)(gnu_hash_addr + 16 + bloom_size * sizeof(uint64_t));
  uint32_t *chains = buckets + nbuckets;

  uint32_t max_sym = 0;
  for (uint32_t i = 0; i < nbuckets; i++) {
    uint32_t b = buckets[i];
    if (b < symoffset)
      continue;
    if (b > max_sym)
      max_sym = b;
    uint32_t idx = b;
    while (1) {
      uint32_t h = chains[idx - symoffset];
      if (idx > max_sym)
        max_sym = idx;
      if (h & 1)
        break;
      idx++;
    }
  }
  return max_sym ? (max_sym + 1) : 0;
}

static uint32_t elf64_sym_count_fallback(const elf64_image_t *img) {
  if (!img || !img->symtab || !img->strtab || !img->syment)
    return 0;
  if (img->strtab <= img->symtab)
    return 0;
  return (uint32_t)((img->strtab - img->symtab) / img->syment);
}

static int elf64_parse_dynamic(elf64_image_t *img, const elf64_ehdr_t *ehdr,
                               const elf64_phdr_t *phdr) {
  if (!img || !ehdr || !phdr)
    return -1;

  const elf64_phdr_t *dyn_ph = nullptr;
  for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
    if (phdr[i].p_type == PT_DYNAMIC) {
      dyn_ph = &phdr[i];
      break;
    }
  }

  if (!dyn_ph) {
    if (ehdr->e_type == ET_EXEC) {
      terminal_printf("[EXEC64] ELF is static, skipping dynamic relocation\n");
      return 0;
    }
    terminal_printf("[EXEC64] missing PT_DYNAMIC\n");
    return -1;
  }

  img->dyn_addr = img->load_base + dyn_ph->p_vaddr;
  img->dyn_size = dyn_ph->p_memsz;

  bool has_strtab = false;
  bool has_symtab = false;
  bool has_rela = false;
  bool has_relasz = false;
  bool has_jmprel = false;
  bool has_pltrelsz = false;
  bool has_syment = false;
  bool has_strsz = false;
  bool has_pltrel = false;
  bool has_relr = false;
  bool has_relrsz = false;
  img->needed_count = 0;

  Elf64_Dyn *dyn = (Elf64_Dyn *)(uintptr_t)img->dyn_addr;
  uint64_t dyn_count = img->dyn_size / sizeof(Elf64_Dyn);
  for (uint64_t i = 0; i < dyn_count; i++) {
    uint64_t tag = (uint64_t)dyn[i].d_tag;
    uint64_t val = dyn[i].d_un.d_val;
    uint64_t ptr = dyn[i].d_un.d_ptr;
    switch (tag) {
    case DT_NULL:
      i = dyn_count;
      break;
    case DT_NEEDED:
      img->needed_count++;
      break;
    case DT_RELA:
      img->rela_addr = (ehdr->e_type == ET_DYN) ? (img->load_base + ptr) : ptr;
      has_rela = true;
      break;
    case DT_RELASZ:
      img->rela_sz = val;
      has_relasz = true;
      break;
    case DT_RELAENT:
      img->rela_ent = val;
      break;
    case DT_JMPREL:
      img->jmprel_addr = (ehdr->e_type == ET_DYN) ? (img->load_base + ptr) : ptr;
      has_jmprel = true;
      break;
    case DT_RELR:
      img->relr_addr = (ehdr->e_type == ET_DYN) ? (img->load_base + ptr) : ptr;
      has_relr = true;
      break;
    case DT_RELRSZ:
      img->relr_sz = val;
      has_relrsz = true;
      break;
    case DT_RELRENT:
      img->relr_ent = val;
      break;
    case DT_PLTRELSZ:
      img->pltrel_sz = val;
      has_pltrelsz = true;
      break;
    case DT_PLTREL:
      img->pltrel_type = val;
      has_pltrel = true;
      break;
    case DT_SYMTAB:
      img->symtab = (ehdr->e_type == ET_DYN) ? (img->load_base + ptr) : ptr;
      has_symtab = true;
      break;
    case DT_SYMENT:
      img->syment = val;
      has_syment = true;
      break;
    case DT_STRTAB:
      img->strtab = (ehdr->e_type == ET_DYN) ? (img->load_base + ptr) : ptr;
      has_strtab = true;
      break;
    case DT_STRSZ:
      img->strsz = val;
      has_strsz = true;
      break;
    case DT_HASH:
      img->hash_addr = (ehdr->e_type == ET_DYN) ? (img->load_base + ptr) : ptr;
      break;
    case DT_GNU_HASH:
      img->gnu_hash_addr = (ehdr->e_type == ET_DYN) ? (img->load_base + ptr) : ptr;
      break;
    default:
      break;
    }
  }

  if (!has_strtab || !img->strtab || !has_symtab || !img->symtab || !has_rela ||
      !has_relasz || !has_syment || !img->syment || !has_strsz ||
      !img->strsz) {
    terminal_printf("[EXEC64] missing required DT_* tags\n");
    return -1;
  }

  /* DT_JMPREL/DT_PLTRELSZ are optional. Many ET_EXEC/ET_DYN binaries only
   * carry .rela.dyn and have no PLT relocation table at all. */
  if (!has_jmprel)
    img->jmprel_addr = 0;
  if (!has_pltrelsz)
    img->pltrel_sz = 0;
  if (!has_relr)
    img->relr_addr = 0;
  if (!has_relrsz)
    img->relr_sz = 0;
  if (!has_pltrel)
    img->pltrel_type = DT_RELA;
  if (img->pltrel_sz > 0 && img->pltrel_type != DT_RELA) {
    terminal_printf("[EXEC64] unsupported DT_PLTREL=%llu\n",
                    (unsigned long long)img->pltrel_type);
    return -1;
  }

  if (!img->syment)
    img->syment = sizeof(Elf64_Sym);
  if (!img->rela_ent)
    img->rela_ent = sizeof(Elf64_Rela);
  if (!img->relr_ent)
    img->relr_ent = sizeof(uint64_t);
  if (!img->sym_count) {
    img->sym_count = elf64_sym_count_from_hash(img->hash_addr);
    if (!img->sym_count)
      img->sym_count = elf64_sym_count_from_gnu_hash(img->gnu_hash_addr);
    if (!img->sym_count)
      img->sym_count = elf64_sym_count_fallback(img);
  }
  return 0;
}

static const Elf64_Sym *elf64_get_sym(const elf64_image_t *img,
                                      uint32_t sym_index) {
  if (!img || !img->symtab || !img->syment)
    return nullptr;
  if (img->sym_count && sym_index >= img->sym_count)
    return nullptr;
  uint64_t addr = img->symtab + (uint64_t)sym_index * img->syment;
  return (const Elf64_Sym *)(uintptr_t)addr;
}

static const char *elf64_sym_name(const elf64_image_t *img,
                                  const Elf64_Sym *sym) {
  if (!img || !sym || !img->strtab || !img->strsz)
    return nullptr;
  if (sym->st_name >= img->strsz)
    return nullptr;
  return (const char *)(uintptr_t)(img->strtab + sym->st_name);
}

static int elf64_resolve_symbol(const elf64_image_t *img, uint32_t sym_index,
                                elf64_image_t **images, int img_count,
                                uint64_t *out_addr, uint64_t *out_size) {
  if (!img || !out_addr)
    return -1;
  const Elf64_Sym *sym = elf64_get_sym(img, sym_index);
  if (!sym)
    return -1;
  if (out_size)
    *out_size = sym->st_size;
  uint8_t bind = ELF64_ST_BIND(sym->st_info);
  if (sym->st_shndx != SHN_UNDEF &&
      (bind == STB_GLOBAL || bind == STB_WEAK)) {
    *out_addr = img->load_base + sym->st_value;
    return 0;
  }

  const char *name = elf64_sym_name(img, sym);
  if (!name)
    return -1;

  auto search_image = [&](elf64_image_t *other) -> int {
    if (!other || !other->symtab)
      return -1;
    uint32_t count = other->sym_count;
    if (!count)
      count = elf64_sym_count_fallback(other);
    for (uint32_t si = 0; si < count; si++) {
      const Elf64_Sym *osym = elf64_get_sym(other, si);
      if (!osym)
        continue;
      if (osym->st_shndx == SHN_UNDEF)
        continue;
      uint8_t obind = ELF64_ST_BIND(osym->st_info);
      if (obind != STB_GLOBAL && obind != STB_WEAK)
        continue;
      const char *oname = elf64_sym_name(other, osym);
      if (!oname)
        continue;
      if (strcmp(oname, name) == 0) {
        *out_addr = other->load_base + osym->st_value;
        if (out_size && osym->st_size)
          *out_size = osym->st_size;
        return 0;
      }
    }
    return -1;
  };

  for (int i = 0; i < img_count; i++) {
    elf64_image_t *other = images[i];
    if (other && other->is_main) {
      if (search_image(other) == 0)
        return 0;
    }
  }

  for (int i = 0; i < img_count; i++) {
    elf64_image_t *other = images[i];
    if (!other || other->is_main)
      continue;
    if (search_image(other) == 0)
      return 0;
  }

  return -1;
}

static elf64_image_t *elf64_resolve_symbol_image(const elf64_image_t *img,
                                                 uint32_t sym_index,
                                                 elf64_image_t **images,
                                                 int img_count,
                                                 uint64_t *out_addr,
                                                 uint64_t *out_size) {
  if (!img || !out_addr)
    return nullptr;
  const Elf64_Sym *sym = elf64_get_sym(img, sym_index);
  if (!sym)
    return nullptr;
  if (out_size)
    *out_size = sym->st_size;
  uint8_t bind = ELF64_ST_BIND(sym->st_info);
  if (sym->st_shndx != SHN_UNDEF &&
      (bind == STB_GLOBAL || bind == STB_WEAK)) {
    *out_addr = img->load_base + sym->st_value;
    return (elf64_image_t *)img;
  }

  const char *name = elf64_sym_name(img, sym);
  if (!name)
    return nullptr;

  auto search_image = [&](elf64_image_t *other) -> elf64_image_t * {
    if (!other || !other->symtab)
      return nullptr;
    uint32_t count = other->sym_count;
    if (!count)
      count = elf64_sym_count_fallback(other);
    for (uint32_t si = 0; si < count; si++) {
      const Elf64_Sym *osym = elf64_get_sym(other, si);
      if (!osym)
        continue;
      if (osym->st_shndx == SHN_UNDEF)
        continue;
      uint8_t obind = ELF64_ST_BIND(osym->st_info);
      if (obind != STB_GLOBAL && obind != STB_WEAK)
        continue;
      const char *oname = elf64_sym_name(other, osym);
      if (!oname)
        continue;
      if (strcmp(oname, name) == 0) {
        *out_addr = other->load_base + osym->st_value;
        if (out_size && osym->st_size)
          *out_size = osym->st_size;
        return other;
      }
    }
    return nullptr;
  };

  for (int i = 0; i < img_count; i++) {
    elf64_image_t *other = images[i];
    if (other && other->is_main) {
      elf64_image_t *found = search_image(other);
      if (found)
        return found;
    }
  }

  for (int i = 0; i < img_count; i++) {
    elf64_image_t *other = images[i];
    if (!other || other->is_main)
      continue;
    elf64_image_t *found = search_image(other);
    if (found)
      return found;
  }

  return nullptr;
}

static int map_user_range_rw(uint64_t start_addr, uint64_t end_addr) {
  uint64_t start = start_addr & ~0xFFFULL;
  uint64_t end = (end_addr + 0xFFFULL) & ~0xFFFULL;
  for (uint64_t va = start; va < end; va += 0x1000ULL) {
    uint64_t phys = paging64_alloc_frame();
    if (!phys)
      return -1;
    if (paging64_map_page(va, phys, 0x7) < 0)
      return -1;
    memset((void *)(uintptr_t)va, 0, 0x1000);
  }
  return 0;
}

static uint64_t init_static_tls(elf64_image_t *images, int img_count) {
  uint64_t total = 0;
  for (int i = 0; i < img_count; i++) {
    elf64_image_t *img = &images[i];
    if (!img->tls_memsz)
      continue;
    uint64_t align = img->tls_align ? img->tls_align : 8;
    total = (total + align - 1) & ~(align - 1);
    total += img->tls_memsz;
    img->tls_block_offset = total;
  }

  if (!total)
    return 0;

  uint64_t fs_base = ELF64_TLS_BASE;
  uint64_t tcb_size = 0x100;
  uint64_t tls_start = fs_base - total;
  uint64_t tls_end = fs_base + tcb_size;
  if (map_user_range_rw(tls_start, tls_end) < 0)
    return 0;

  memset((void *)(uintptr_t)tls_start, 0, (size_t)(tls_end - tls_start));
  *(uint64_t *)(uintptr_t)fs_base = fs_base;
  *(uint64_t *)(uintptr_t)(fs_base + 8) = 0;

  for (int i = 0; i < img_count; i++) {
    elf64_image_t *img = &images[i];
    if (!img->tls_memsz || !img->tls_block_offset)
      continue;
    uint64_t block = fs_base - img->tls_block_offset;
    if (img->file_data && img->tls_filesz) {
      memcpy((void *)(uintptr_t)block,
             img->file_data + img->tls_offset,
             (size_t)img->tls_filesz);
    }
  }

  return fs_base;
}

static uint64_t elf64_call_ifunc_resolver(uint64_t resolver_addr) {
  if (!resolver_addr)
    return 0;
  uint64_t (*resolver)(void) = (uint64_t(*)(void))(uintptr_t)resolver_addr;
  return resolver();
}

static int elf64_apply_relocations(elf64_image_t *img,
                                   elf64_image_t **images,
                                   int img_count,
                                   bool full) {
  if (!img)
    return -1;

  auto apply_rela = [&](uint64_t rela_base, uint64_t rela_size,
                        uint64_t ent_size) -> int {
    if (!rela_base || rela_size == 0)
      return 0;
    if (!ent_size)
      ent_size = sizeof(Elf64_Rela);
    uint64_t count = rela_size / ent_size;
    for (uint64_t i = 0; i < count; i++) {
      uint64_t rela_addr = rela_base + i * ent_size;
      Elf64_Rela *rela = (Elf64_Rela *)(uintptr_t)rela_addr;
      if (!rela)
        return -1;
      uint64_t reloc_va = img->load_base + rela->r_offset;
      uint64_t *where = (uint64_t *)(uintptr_t)reloc_va;
      if (!where)
        return -1;
      uint32_t type = ELF64_R_TYPE(rela->r_info);
      uint32_t symi = ELF64_R_SYM(rela->r_info);
      const Elf64_Sym *sym = nullptr;
      const char *sym_name = nullptr;
      if (symi) {
        sym = elf64_get_sym(img, symi);
        sym_name = sym ? elf64_sym_name(img, sym) : nullptr;
      }

      terminal_printf("[EXEC64] reloc type=%u addr=%p sym=%s\n", type,
                      (void *)(uintptr_t)reloc_va,
                      sym_name ? sym_name : "(none)");

      if (type == R_X86_64_NONE) {
        continue;
      }

      if (type == R_X86_64_RELATIVE) {
        *where = img->load_base + (uint64_t)rela->r_addend;
        continue;
      }

      if (type == R_X86_64_IRELATIVE) {
        uint64_t resolver_addr = img->load_base + (uint64_t)rela->r_addend;
        *where = elf64_call_ifunc_resolver(resolver_addr);
        continue;
      }

      if (type == R_X86_64_TPOFF64 && symi == 0) {
        if (!img->tls_block_offset)
          exec64_abortf("local tpoff without TLS block");
        *where = (uint64_t)rela->r_addend - img->tls_block_offset;
        continue;
      }

      if (type == R_X86_64_DTPMOD64 && symi == 0) {
        *where = 1;
        continue;
      }

      if (type == R_X86_64_DTPOFF64 && symi == 0) {
        *where = (uint64_t)rela->r_addend;
        continue;
      }

      if (!full)
        continue;

      uint64_t sym_addr = 0;
      uint64_t sym_size = 0;
      elf64_image_t *def_img =
          elf64_resolve_symbol_image(img, symi, images, img_count, &sym_addr,
                                     &sym_size);
      if (!def_img) {
        uint8_t bind = sym ? ELF64_ST_BIND(sym->st_info) : STB_GLOBAL;
        if (sym && bind == STB_WEAK) {
          sym_addr = 0;
          sym_size = 0;
        } else {
          exec64_abortf("reloc unresolved for symbol: %s",
                        sym_name ? sym_name : "(unknown)");
        }
      }

      if (sym && ELF64_ST_TYPE(sym->st_info) == STT_GNU_IFUNC && sym_addr) {
        sym_addr = elf64_call_ifunc_resolver(sym_addr);
      }

      switch (type) {
      case R_X86_64_64:
      case R_X86_64_GLOB_DAT:
      case R_X86_64_JUMP_SLOT:
        *where = sym_addr + (uint64_t)rela->r_addend;
        break;
      case R_X86_64_COPY: {
        if (!sym_addr)
          exec64_abortf("copy reloc unresolved for symbol: %s",
                        sym_name ? sym_name : "(unknown)");
        uint64_t copy_sz = sym && sym->st_size ? sym->st_size : sym_size;
        if (!copy_sz)
          copy_sz = sym_size;
        if (!copy_sz)
          exec64_abortf("copy reloc has zero size for symbol: %s",
                        sym_name ? sym_name : "(unknown)");
        memcpy((void *)(uintptr_t)where, (const void *)(uintptr_t)sym_addr,
               (size_t)copy_sz);
        break;
      }
      case R_X86_64_DTPMOD64:
        *where = def_img ? 1 : 0;
        break;
      case R_X86_64_DTPOFF64:
        if (!def_img)
          exec64_abortf("dtpoff unresolved for symbol: %s",
                        sym_name ? sym_name : "(unknown)");
        *where = (sym_addr - def_img->load_base) + (uint64_t)rela->r_addend;
        break;
      case R_X86_64_TPOFF64: {
        elf64_image_t *tls_img = def_img ? def_img : img;
        if (!tls_img->tls_block_offset)
          exec64_abortf("tpoff without TLS block for symbol: %s",
                        sym_name ? sym_name : "(unknown)");
        uint64_t sym_value = 0;
        if (sym && symi)
          sym_value = sym->st_value;
        *where = sym_value + (uint64_t)rela->r_addend - tls_img->tls_block_offset;
        break;
      }
      default:
        exec64_abortf("unsupported relocation type=%u", type);
      }
    }
    return 0;
  };

  auto apply_relr = [&](uint64_t relr_base, uint64_t relr_size,
                        uint64_t ent_size) -> int {
    if (!relr_base || relr_size == 0)
      return 0;
    if (!ent_size)
      ent_size = sizeof(uint64_t);
    uint64_t count = relr_size / ent_size;
    uint64_t next_off = 0;
    for (uint64_t i = 0; i < count; i++) {
      uint64_t *entryp = (uint64_t *)(uintptr_t)(relr_base + i * ent_size);
      uint64_t entry = *entryp;
      if ((entry & 1ULL) == 0) {
        uint64_t *where = (uint64_t *)(uintptr_t)(img->load_base + entry);
        *where += img->load_base;
        next_off = entry + sizeof(uint64_t);
        continue;
      }
      uint64_t bitmap = entry >> 1;
      for (uint64_t bit = 0; bit < 63; ++bit) {
        if ((bitmap & (1ULL << bit)) == 0)
          continue;
        uint64_t off = next_off + bit * sizeof(uint64_t);
        uint64_t *where = (uint64_t *)(uintptr_t)(img->load_base + off);
        *where += img->load_base;
      }
      next_off += 63 * sizeof(uint64_t);
    }
    return 0;
  };

  if (img->relr_sz > 0 && img->relr_addr) {
    if (apply_relr(img->relr_addr, img->relr_sz, img->relr_ent) < 0)
      return -1;
  }

  if (apply_rela(img->rela_addr, img->rela_sz, img->rela_ent) < 0)
    return -1;

  if (img->pltrel_sz > 0 && img->jmprel_addr) {
    if (img->pltrel_type != DT_RELA) {
      exec64_abortf("unsupported PLT relocation type=%llu",
                    (unsigned long long)img->pltrel_type);
    }
    if (apply_rela(img->jmprel_addr, img->pltrel_sz, img->rela_ent) < 0)
      return -1;
  }

  return 0;
}

/* Removed all dynamic symbol and relocation logic. The kernel MUST NOT do this. */

static int load_elf64_image(const uint8_t *file_data, size_t file_size,
                            uint64_t load_base, elf64_image_t *out_img,
                            char *interp_buf, size_t interp_buf_sz) {
  if (!file_data || file_size < sizeof(elf64_ehdr_t)) return -1;
  if (out_img) {
    memset(out_img, 0, sizeof(*out_img));
  }
  const elf64_ehdr_t *eh = (const elf64_ehdr_t *)file_data;
  if (!elf64_is_valid(file_data, (uint32_t)file_size)) return -1;
  if (eh->e_machine != EM_X86_64 || (eh->e_type != ET_EXEC && eh->e_type != ET_DYN)) return -1;

  const elf64_phdr_t *phdr = (const elf64_phdr_t *)(file_data + eh->e_phoff);
  uint64_t image_end = 0;
  uint64_t phdr_addr = 0;
  uint64_t base = load_base;
  bool base_set = false;
  bool has_dynamic = false;
  bool has_interp = false;

  for (uint16_t i = 0; i < eh->e_phnum; i++) {
    if (phdr[i].p_type != PT_LOAD)
      continue;
    uint64_t aligned_vaddr = phdr[i].p_vaddr & ~0xFFFULL;
    uint64_t mapped_start = load_base + aligned_vaddr;
    base = mapped_start - aligned_vaddr;
    base_set = true;
    break;
  }
  if (!base_set && eh->e_phnum > 0)
    base = load_base;

  for (uint16_t i = 0; i < eh->e_phnum; i++) {
    if (phdr[i].p_type == PT_PHDR) phdr_addr = base + phdr[i].p_vaddr;
    if (phdr[i].p_type == PT_DYNAMIC) has_dynamic = true;
    if (phdr[i].p_type == PT_INTERP) has_interp = true;
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

    uint64_t seg_vaddr = base + phdr[i].p_vaddr;
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
    uint64_t seg_vaddr = base + phdr[i].p_vaddr;
    uint64_t start = seg_vaddr & ~0xFFFULL;
    uint64_t end = (seg_vaddr + phdr[i].p_memsz + 0xFFFULL) & ~0xFFFULL;
    
    uint64_t map_flags = 0x5; /* P | USER */
    if (phdr[i].p_flags & PF_W) map_flags |= 0x2; /* RW */
    /* If the system supports NX, we would handle PF_X here via bit 63. Chrysalis paging64 doesn't yet, so we ignore PF_X. */
    
    for (uint64_t va = start; va < end; va += 0x1000) {
      paging64_protect_page(va, map_flags);
    }
    if (user64_register_vma(start, end, phdr_flags_to_user_prot(phdr[i].p_flags),
                            0) < 0) {
      terminal_printf("[EXEC64] failed to register PT_LOAD VMA %p-%p\n",
                      (void *)(uintptr_t)start, (void *)(uintptr_t)end);
      return -1;
    }
  }

  if (out_img) {
    out_img->load_base = base;
    out_img->entry = base + eh->e_entry;
    out_img->image_end = image_end;
    out_img->phdr = phdr_addr ? phdr_addr : (base + eh->e_phoff);
    out_img->phent = eh->e_phentsize;
    out_img->phnum = eh->e_phnum;
    if (has_interp && interp_buf && interp_buf_sz > 0) {
      /* For ET_EXEC/ET_DYN binaries with PT_INTERP, the kernel only maps the
       * main image and hands off dynamic linking to ld-linux. Do not reject
       * the exec here because of a kernel-side DT_* parser limitation. */
      return 0;
    }
    if (eh->e_type == ET_DYN || has_dynamic) {
      if (elf64_parse_dynamic(out_img, eh, phdr) < 0)
        return -1;
    } else {
      terminal_printf("[EXEC64] ELF is static, skipping dynamic relocation\n");
    }
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

  int auxv_entries = 17; /* includes AT_NULL; add AT_EXECFN if argc>0 */
  if (argc > 0)
    auxv_entries++;

  uint64_t total_entries = 2 /* AT_RANDOM payload */ + 1 /* argc */ +
                           (uint64_t)argc + 1 /* argv NULL */ +
                           (uint64_t)envc + 1 /* envp NULL */ +
                           (uint64_t)auxv_entries * 2;
  if ((total_entries & 1ULL) != 0) {
    sp -= 8; /* ELF process entry requires rsp % 16 == 0. */
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
  push64(0); push64(AT_HWCAP2);
  push64(0); push64(AT_EUID);
  push64(0); push64(AT_UID);
  push64(0); push64(AT_EGID);
  push64(0); push64(AT_GID);
  push64(0); push64(AT_FLAGS);
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

static void exec64_reset_task_runtime(task64_t *t) {
  if (!t)
    return;
  t->sig_pending = 0;
  t->sig_mask = 0;
  memset(t->sig_actions, 0, sizeof(t->sig_actions));
  t->sig_saved_rip = 0;
  t->sig_saved_rsp = 0;
  t->sig_active = 0;
  t->clear_tid_addr = 0;
  t->robust_list_head = 0;
  t->robust_list_len = 0;
  memset(t->epoll_table, 0, sizeof(t->epoll_table));
  t->gs.user_gs_base = 0;
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

static elf64_image_t *elf64_find_loaded_image(elf64_image_t *images, int img_count,
                                              const char *name) {
  if (!name)
    return nullptr;
  const char *base = name;
  for (const char *p = name; *p; ++p) {
    if (*p == '/' || *p == '\\')
      base = p + 1;
  }
  for (int i = 0; i < img_count; i++) {
    if (!images[i].name)
      continue;
    if (strcmp(images[i].name, name) == 0)
      return &images[i];
    const char *other = images[i].name;
    const char *other_base = other;
    for (const char *p = other; *p; ++p) {
      if (*p == '/' || *p == '\\')
        other_base = p + 1;
    }
    if (strcmp(other_base, base) == 0)
      return &images[i];
  }
  return nullptr;
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
    
    terminal_printf("[EXEC64]   trying library path: %s\n", path);

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
  user64_reset_process();

  elf64_image_t images[32];
  elf64_image_t *image_ptrs[32];
  int img_count = 0;
  char interp_path[256];
  interp_path[0] = 0;

  const elf64_ehdr_t *eh = (const elf64_ehdr_t *)kfile;
  uint64_t main_base = (eh && eh->e_type == ET_DYN) ? ELF64_DYN_BASE : 0;

  serial_write_string("[EXEC64] loading main ELF\r\n");

  if (load_elf64_image(kfile, file_size, main_base, &images[0], interp_path, sizeof(interp_path)) < 0) {
    terminal_printf("[EXEC64] load_elf64_image FAILED\n");
    kfree(kfile);
    paging64_restore_boot_pml4();
    return -1;
  }
  img_count = 1;
  images[0].name = image_path ? image_path : "(main)";
  images[0].is_main = 1;
  image_ptrs[0] = &images[0];
  terminal_printf("[EXEC64] main image loaded: base=%p entry=%p\n",
                  (void *)(uintptr_t)images[0].load_base,
                  (void *)(uintptr_t)images[0].entry);

  if (interp_path[0] && k_exec64_kernel_link_dynamic) {
    const elf64_phdr_t *main_phdr =
        (const elf64_phdr_t *)(kfile + eh->e_phoff);
    if (elf64_parse_dynamic(&images[0], eh, main_phdr) < 0) {
      terminal_printf("[EXEC64] main dynamic parse failed under kernel-link path\n");
      kfree(kfile);
      paging64_restore_boot_pml4();
      return -1;
    }
    terminal_printf("[EXEC64] PT_INTERP present, using kernel-link path\n");
  }

  if (interp_path[0] && !k_exec64_kernel_link_dynamic) {
    terminal_printf("[EXEC64] interpreter requested: %s\n", interp_path);

    const uint8_t *interp_data = nullptr;
    size_t interp_size = 0;
    int interp_owned = 0;
    if (read_exec64_path(interp_path, &interp_data, &interp_size, &interp_owned) <
        0) {
      terminal_printf("[EXEC64] failed to load interpreter: %s\n", interp_path);
      kfree(kfile);
      paging64_restore_boot_pml4();
      return -1;
    }

    if (load_elf64_image(interp_data, interp_size, ELF64_INTERP_BASE,
                         &images[1], nullptr, 0) < 0) {
      terminal_printf("[EXEC64] failed to map interpreter: %s\n", interp_path);
      if (interp_owned)
        kfree((void *)interp_data);
      kfree(kfile);
      paging64_restore_boot_pml4();
      return -1;
    }

    images[1].file_data = interp_data;
    images[1].file_size = interp_size;
    images[1].owned = interp_owned;
    images[1].name = interp_path;
    images[1].is_main = 0;
    image_ptrs[1] = &images[1];
    img_count = 2;
    terminal_printf("[EXEC64] interpreter loaded: base=%p entry=%p\n",
                    (void *)(uintptr_t)images[1].load_base,
                    (void *)(uintptr_t)images[1].entry);
  }

  char base_dir_buf[256];
  const char *base_dir = basename_dir(image_path, base_dir_buf, sizeof(base_dir_buf));

  if (!interp_path[0] || k_exec64_kernel_link_dynamic) {
    int lib_index = 0;
    for (int idx = 0; idx < img_count; idx++) {
      const char *needed[32];
      int needed_count = elf64_collect_needed(&images[idx], needed, 32);
      for (int i = 0; i < needed_count; i++) {
        const char *libname = needed[i];
        if (!libname || elf64_is_loaded(images, img_count, libname))
          continue;

        terminal_printf("[EXEC64] DT_NEEDED: %s\n", libname);

        const uint8_t *lib_data = nullptr;
        size_t lib_size = 0;
        int lib_owned = 0;
        if (elf64_find_library(libname, base_dir, &lib_data, &lib_size, &lib_owned) < 0) {
          terminal_printf("[EXEC64] missing shared library: %s\n", libname);
          kfree(kfile);
          paging64_restore_boot_pml4();
          return -1;
        }

        if (img_count >= (int)(sizeof(images) / sizeof(images[0]))) {
          terminal_printf("[EXEC64] too many shared libraries\n");
          kfree(kfile);
          paging64_restore_boot_pml4();
          return -1;
        }

        const elf64_ehdr_t *lib_eh = (const elf64_ehdr_t *)lib_data;
        uint64_t lib_base = (lib_eh && lib_eh->e_type == ET_DYN)
                                ? (ELF64_LIB_BASE + ELF64_LIB_STRIDE * (uint64_t)lib_index)
                                : 0;

        if (load_elf64_image(lib_data, lib_size, lib_base, &images[img_count], nullptr, 0) < 0) {
          terminal_printf("[EXEC64] failed to load library: %s\n", libname);
          if (lib_owned)
            kfree((void *)lib_data);
          kfree(kfile);
          paging64_restore_boot_pml4();
          return -1;
        }

        images[img_count].file_data = lib_data;
        images[img_count].file_size = lib_size;
        images[img_count].owned = lib_owned;
        images[img_count].name = libname;
        image_ptrs[img_count] = &images[img_count];
        terminal_printf("[EXEC64] loaded %s: base=%p entry=%p\n", libname,
                        (void *)(uintptr_t)images[img_count].load_base,
                        (void *)(uintptr_t)images[img_count].entry);
        img_count++;
        lib_index++;
      }
    }
  }

  kfree(kfile);

  uint64_t fs_base = 0;
  if (!interp_path[0] || k_exec64_kernel_link_dynamic) {
    fs_base = init_static_tls(images, img_count);
    terminal_printf("[EXEC64] tls fs_base=%p\n", (void *)(uintptr_t)fs_base);

    for (int i = 0; i < img_count; i++) {
      if (elf64_apply_relocations(&images[i], image_ptrs, img_count, true) < 0) {
        terminal_printf("[EXEC64] relocations failed\n");
        paging64_restore_boot_pml4();
        return -1;
      }
    }
  } else {
    elf64_image_t *interp_img =
        elf64_find_loaded_image(images, img_count, interp_path);
    if (!interp_img)
      interp_img = &images[1];
    elf64_image_t *interp_only[1] = {interp_img};
    if (elf64_apply_relocations(interp_img, interp_only, 1, true) < 0) {
      terminal_printf("[EXEC64] interpreter relocations failed\n");
      paging64_restore_boot_pml4();
      return -1;
    }
  }

  uint64_t main_image_end = images[0].image_end;

  uint64_t stack_top = ELF64_STACK_TOP;
  if (map_user_stack(stack_top, ELF64_STACK_SIZE) < 0) {
    paging64_restore_boot_pml4();
    return -1;
  }
  if (user64_register_vma(stack_top - ELF64_STACK_SIZE, stack_top,
                          USER64_PROT_READ | USER64_PROT_WRITE, 0) < 0) {
    terminal_printf("[EXEC64] failed to register stack VMA\n");
    paging64_restore_boot_pml4();
    return -1;
  }
  terminal_printf("[EXEC64] stack mapped\n");

  uint64_t at_base = 0;
  uint64_t entry = images[0].entry;
  if (interp_path[0] && !k_exec64_kernel_link_dynamic) {
    elf64_image_t *interp_img =
        elf64_find_loaded_image(images, img_count, interp_path);
    if (!interp_img)
      interp_img = elf64_find_loaded_image(images, img_count,
                                           "ld-linux-x86-64.so.2");
    if (!interp_img) {
      terminal_printf("[EXEC64] interpreter image not loaded: %s\n", interp_path);
      paging64_restore_boot_pml4();
      return -1;
    }
    at_base = interp_img->load_base;
    entry = interp_img->entry;
    fs_base = 0;
    terminal_printf("[EXEC64] interpreter entry=%p base=%p\n",
                    (void *)(uintptr_t)entry,
                    (void *)(uintptr_t)at_base);
  } else if (interp_path[0]) {
    terminal_printf("[EXEC64] direct entry for PT_INTERP image: entry=%p\n",
                    (void *)(uintptr_t)entry);
  }
  
  uint64_t rsp = build_user_stack(kargv, kenvp, stack_top,
                                  images[0].entry, images[0].phdr,
                                  images[0].phent, images[0].phnum, at_base);

  terminal_printf("[EXEC64] stack built: rsp=%p align=%u entry=%p\n",
                  (void *)(uintptr_t)rsp, (unsigned)(rsp & 0xF),
                  (void *)(uintptr_t)entry);

  dump_user_stack_layout(rsp);

  for (int i = 1; i < img_count; i++) {
    if (images[i].owned)
      kfree((void *)images[i].file_data);
  }

  free_string_array(kargv);
  free_string_array(kenvp);

  exec64_reset_task_runtime(t);
  user64_init_process(main_image_end);
  if (image_path) {
    strncpy(t->exe_path, image_path, sizeof(t->exe_path) - 1);
    t->exe_path[sizeof(t->exe_path) - 1] = 0;
  }
  task64_set_user_stack(rsp);
  t->gs.fs_base = fs_base;

  terminal_printf("[EXEC64] entering user mode: rip=%p rsp=%p\n",
                  (void *)entry, (void *)rsp);

  syscall64_prepare_exec_transition();
  void (*enter_fn)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) =
      user64_enter;
  register uint64_t r8 asm("r8") = 0;
  asm volatile("jmp *%0"
               :
               : "r"(enter_fn), "D"(entry), "S"(rsp), "d"(0x202), "c"(fs_base),
                 "r"(r8)
               : "memory");
  __builtin_unreachable();
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
