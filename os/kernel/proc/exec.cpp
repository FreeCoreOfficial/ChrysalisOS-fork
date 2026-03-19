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
#include "../elf/elf64.h"
#include "../arch/x86_64/longmode.h"
#include "exec64.h"
#include "../string.h"
#include "../terminal.h"

/* FAT32 Driver API */
extern "C" int fat32_read_file(const char *path, void *buf, uint32_t max_size);
extern "C" int32_t fat32_get_file_size(const char *path);
extern "C" void serial(const char *fmt, ...);

/* ELF Header Definitions */
#define ELF_MAGIC 0x464C457F
#ifndef ELFCLASS32
#define ELFCLASS32 1
#endif
#ifndef ELFCLASS64
#define ELFCLASS64 2
#endif
#ifndef EM_386
#define EM_386 3
#endif
#ifndef EM_X86_64
#define EM_X86_64 62
#endif
#ifndef ET_EXEC
#define ET_EXEC 2
#endif
#ifndef ET_DYN
#define ET_DYN 3
#endif

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
#define PT_DYNAMIC 2
#define PT_INTERP 3

typedef struct {
  int32_t d_tag;
  union {
    uint32_t d_val;
    uint32_t d_ptr;
  } d_un;
} Elf32_Dyn;

typedef struct {
  uint32_t st_name;
  uint32_t st_value;
  uint32_t st_size;
  uint8_t st_info;
  uint8_t st_other;
  uint16_t st_shndx;
} Elf32_Sym;

typedef struct {
  uint32_t r_offset;
  uint32_t r_info;
} Elf32_Rel;

typedef struct {
  uint32_t r_offset;
  uint32_t r_info;
  int32_t r_addend;
} Elf32_Rela;

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
#define DT_RELSZ 18
#define DT_RELENT 19
#define DT_PLTREL 20
#define DT_JMPREL 23
#define DT_PLTRELSZ 2
#define DT_GNU_HASH 0x6ffffef5

#define R_386_32 1
#define R_386_GLOB_DAT 6
#define R_386_JMP_SLOT 7
#define R_386_RELATIVE 8

#define ELF32_R_SYM(i) ((i) >> 8)
#define ELF32_R_TYPE(i) ((uint8_t)(i))
#define ELF32_ST_BIND(i) ((uint8_t)((i) >> 4))

#define STB_LOCAL 0
#define STB_GLOBAL 1
#define STB_WEAK 2

#define ELF32_DYN_BASE 0x40000000u
#define ELF32_STACK_TOP 0xBFF00000u
#define ELF32_STACK_SIZE (64 * 1024u)

/* Helper to read file content into a buffer */
static const uint8_t *read_executable(const char *path, size_t *out_size,
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

  return nullptr;
}

static const char *basename_ptr(const char *path) {
  if (!path)
    return "app";
  const char *base = path;
  for (const char *p = path; *p; ++p) {
    if (*p == '/' || *p == '\\')
      base = p + 1;
  }
  return base;
}

static uint8_t *as_translate_ptr(address_space_t *as, uint32_t vaddr) {
  if (!as || !as->page_directory)
    return nullptr;
  uint32_t page = vaddr & PAGE_FRAME_MASK;
  uint32_t *pte = get_pte_for(as->page_directory, page, 0);
  if (!pte || !(*pte & PAGE_PRESENT))
    return nullptr;
  uint32_t phys_page = *pte & PAGE_FRAME_MASK;
  return (uint8_t *)(uintptr_t)(phys_page + KERNEL_BASE +
                                (vaddr & (PAGE_SIZE - 1)));
}

typedef struct {
  const char *name;
  address_space_t *as;
  const uint8_t *file_data;
  size_t file_size;
  int owned;
  uint32_t load_base;
  uint32_t entry;
  uint32_t image_end;
  uint32_t phdr;
  uint32_t phent;
  uint32_t phnum;
  int is_main;

  uint32_t dyn_addr;
  uint32_t dyn_size;
  uint32_t symtab;
  uint32_t strtab;
  uint32_t strsz;
  uint32_t syment;
  uint32_t rel_addr;
  uint32_t rel_sz;
  uint32_t rel_ent;
  uint32_t rela_addr;
  uint32_t rela_sz;
  uint32_t rela_ent;
  uint32_t jmprel_addr;
  uint32_t pltrel_sz;
  uint32_t pltrel_type;
  uint32_t hash_addr;
  uint32_t gnu_hash_addr;
  uint32_t sym_count;
} elf32_image_t;

static uint32_t elf32_sym_count_from_hash(address_space_t *as,
                                          uint32_t hash_addr) {
  if (!hash_addr)
    return 0;
  uint32_t *hash = (uint32_t *)as_translate_ptr(as, hash_addr);
  if (!hash)
    return 0;
  return hash[1];
}

static uint32_t elf32_sym_count_from_gnu_hash(address_space_t *as,
                                              uint32_t gnu_hash_addr) {
  if (!gnu_hash_addr)
    return 0;
  uint32_t *hdr = (uint32_t *)as_translate_ptr(as, gnu_hash_addr);
  if (!hdr)
    return 0;
  uint32_t nbuckets = hdr[0];
  uint32_t symoffset = hdr[1];
  uint32_t bloom_size = hdr[2];
  if (nbuckets == 0)
    return 0;

  uint32_t *bloom = (uint32_t *)as_translate_ptr(as, gnu_hash_addr + 16);
  if (!bloom)
    return 0;
  uint32_t *buckets = bloom + bloom_size;
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

static uint32_t elf32_sym_count_fallback(const elf32_image_t *img) {
  if (!img || !img->symtab || !img->strtab || !img->syment)
    return 0;
  if (img->strtab <= img->symtab)
    return 0;
  return (img->strtab - img->symtab) / img->syment;
}

static int elf32_parse_dynamic(elf32_image_t *img, const Elf32_Ehdr *ehdr,
                               const Elf32_Phdr *phdr) {
  if (!img || !ehdr || !phdr)
    return -1;

  const Elf32_Phdr *dyn_ph = nullptr;
  for (int i = 0; i < ehdr->e_phnum; i++) {
    if (phdr[i].p_type == PT_DYNAMIC) {
      dyn_ph = &phdr[i];
      break;
    }
  }

  if (!dyn_ph)
    return 0;

  img->dyn_addr = img->load_base + dyn_ph->p_vaddr;
  img->dyn_size = dyn_ph->p_memsz;

  Elf32_Dyn *dyn = (Elf32_Dyn *)as_translate_ptr(img->as, img->dyn_addr);
  if (!dyn)
    return -1;
  uint32_t dyn_count = img->dyn_size / sizeof(Elf32_Dyn);

  for (uint32_t i = 0; i < dyn_count; i++) {
    uint32_t tag = (uint32_t)dyn[i].d_tag;
    uint32_t val = dyn[i].d_un.d_val;
    uint32_t ptr = dyn[i].d_un.d_ptr;
    switch (tag) {
    case DT_NULL:
      i = dyn_count;
      break;
    case DT_REL:
      img->rel_addr = (ehdr->e_type == ET_DYN) ? (img->load_base + ptr) : ptr;
      break;
    case DT_RELSZ:
      img->rel_sz = val;
      break;
    case DT_RELENT:
      img->rel_ent = val;
      break;
    case DT_RELA:
      img->rela_addr = (ehdr->e_type == ET_DYN) ? (img->load_base + ptr) : ptr;
      break;
    case DT_RELASZ:
      img->rela_sz = val;
      break;
    case DT_RELAENT:
      img->rela_ent = val;
      break;
    case DT_JMPREL:
      img->jmprel_addr =
          (ehdr->e_type == ET_DYN) ? (img->load_base + ptr) : ptr;
      break;
    case DT_PLTRELSZ:
      img->pltrel_sz = val;
      break;
    case DT_PLTREL:
      img->pltrel_type = val;
      break;
    case DT_SYMTAB:
      img->symtab = (ehdr->e_type == ET_DYN) ? (img->load_base + ptr) : ptr;
      break;
    case DT_SYMENT:
      img->syment = val;
      break;
    case DT_STRTAB:
      img->strtab = (ehdr->e_type == ET_DYN) ? (img->load_base + ptr) : ptr;
      break;
    case DT_STRSZ:
      img->strsz = val;
      break;
    case DT_HASH:
      img->hash_addr = (ehdr->e_type == ET_DYN) ? (img->load_base + ptr) : ptr;
      break;
    case DT_GNU_HASH:
      img->gnu_hash_addr =
          (ehdr->e_type == ET_DYN) ? (img->load_base + ptr) : ptr;
      break;
    default:
      break;
    }
  }

  if (!img->syment)
    img->syment = sizeof(Elf32_Sym);
  if (!img->rel_ent)
    img->rel_ent = sizeof(Elf32_Rel);
  if (!img->rela_ent)
    img->rela_ent = sizeof(Elf32_Rela);
  if (!img->sym_count) {
    img->sym_count = elf32_sym_count_from_hash(img->as, img->hash_addr);
    if (!img->sym_count)
      img->sym_count = elf32_sym_count_from_gnu_hash(img->as,
                                                     img->gnu_hash_addr);
    if (!img->sym_count)
      img->sym_count = elf32_sym_count_fallback(img);
  }
  return 0;
}

static const Elf32_Sym *elf32_get_sym(const elf32_image_t *img,
                                      uint32_t sym_index) {
  if (!img || !img->symtab || !img->syment)
    return nullptr;
  if (img->sym_count && sym_index >= img->sym_count)
    return nullptr;
  uint32_t addr = img->symtab + sym_index * img->syment;
  return (const Elf32_Sym *)as_translate_ptr(img->as, addr);
}

static const char *elf32_sym_name(const elf32_image_t *img,
                                  const Elf32_Sym *sym) {
  if (!img || !sym || !img->strtab || !img->strsz)
    return nullptr;
  if (sym->st_name >= img->strsz)
    return nullptr;
  return (const char *)as_translate_ptr(img->as, img->strtab + sym->st_name);
}

static int elf32_resolve_symbol(const elf32_image_t *img, uint32_t sym_index,
                                elf32_image_t **images, int img_count,
                                uint32_t *out_addr) {
  if (!img || !out_addr)
    return -1;
  const Elf32_Sym *sym = elf32_get_sym(img, sym_index);
  if (!sym)
    return -1;
  if (sym->st_shndx != 0) {
    *out_addr = img->load_base + sym->st_value;
    return 0;
  }

  const char *name = elf32_sym_name(img, sym);
  if (!name)
    return -1;

  for (int i = 0; i < img_count; i++) {
    elf32_image_t *other = images[i];
    if (!other || !other->symtab)
      continue;
    uint32_t count = other->sym_count;
    if (!count)
      count = elf32_sym_count_fallback(other);
    for (uint32_t si = 0; si < count; si++) {
      const Elf32_Sym *osym = elf32_get_sym(other, si);
      if (!osym || osym->st_shndx == 0)
        continue;
      const char *oname = elf32_sym_name(other, osym);
      if (!oname)
        continue;
      if (strcmp(oname, name) == 0) {
        *out_addr = other->load_base + osym->st_value;
        return 0;
      }
    }
  }

  uint8_t bind = ELF32_ST_BIND(sym->st_info);
  if (bind == STB_WEAK) {
    *out_addr = 0;
    return 0;
  }
  return -1;
}

static int elf32_apply_relocations(elf32_image_t *img, elf32_image_t **images,
                                   int img_count) {
  if (!img)
    return -1;

  auto apply_rel = [&](uint32_t rel_base, uint32_t rel_size,
                       uint32_t ent_size) -> int {
    if (!rel_base || rel_size == 0)
      return 0;
    uint32_t count = rel_size / ent_size;
    for (uint32_t i = 0; i < count; i++) {
      uint32_t rel_addr = rel_base + i * ent_size;
      Elf32_Rel *rel = (Elf32_Rel *)as_translate_ptr(img->as, rel_addr);
      if (!rel)
        return -1;
      uint32_t reloc_va = img->load_base + rel->r_offset;
      uint32_t *where =
          (uint32_t *)as_translate_ptr(img->as, reloc_va);
      if (!where)
        return -1;
      uint32_t type = ELF32_R_TYPE(rel->r_info);
      uint32_t symi = ELF32_R_SYM(rel->r_info);
      if (type == R_386_RELATIVE) {
        *where += img->load_base;
        continue;
      }

      uint32_t sym_addr = 0;
      if (elf32_resolve_symbol(img, symi, images, img_count, &sym_addr) < 0)
        return -1;

      switch (type) {
      case R_386_32:
        *where = sym_addr + *where;
        break;
      case R_386_GLOB_DAT:
      case R_386_JMP_SLOT:
        *where = sym_addr;
        break;
      default:
        return -1;
      }
    }
    return 0;
  };

  auto apply_rela = [&](uint32_t rela_base, uint32_t rela_size,
                        uint32_t ent_size) -> int {
    if (!rela_base || rela_size == 0)
      return 0;
    uint32_t count = rela_size / ent_size;
    for (uint32_t i = 0; i < count; i++) {
      uint32_t rela_addr = rela_base + i * ent_size;
      Elf32_Rela *rela = (Elf32_Rela *)as_translate_ptr(img->as, rela_addr);
      if (!rela)
        return -1;
      uint32_t reloc_va = img->load_base + rela->r_offset;
      uint32_t *where =
          (uint32_t *)as_translate_ptr(img->as, reloc_va);
      if (!where)
        return -1;
      uint32_t type = ELF32_R_TYPE(rela->r_info);
      uint32_t symi = ELF32_R_SYM(rela->r_info);
      if (type == R_386_RELATIVE) {
        *where = img->load_base + (uint32_t)rela->r_addend;
        continue;
      }

      uint32_t sym_addr = 0;
      if (elf32_resolve_symbol(img, symi, images, img_count, &sym_addr) < 0)
        return -1;

      switch (type) {
      case R_386_32:
      case R_386_GLOB_DAT:
      case R_386_JMP_SLOT:
        *where = sym_addr + (uint32_t)rela->r_addend;
        break;
      default:
        return -1;
      }
    }
    return 0;
  };

  if (apply_rel(img->rel_addr, img->rel_sz, img->rel_ent) < 0)
    return -1;
  if (apply_rela(img->rela_addr, img->rela_sz, img->rela_ent) < 0)
    return -1;

  if (img->pltrel_sz > 0 && img->jmprel_addr) {
    if (img->pltrel_type == DT_REL) {
      if (apply_rel(img->jmprel_addr, img->pltrel_sz, img->rel_ent) < 0)
        return -1;
    } else if (img->pltrel_type == DT_RELA) {
      if (apply_rela(img->jmprel_addr, img->pltrel_sz, img->rela_ent) < 0)
        return -1;
    }
  }
  return 0;
}

static const char *basename_dir32(const char *path, char *buf, size_t buf_sz) {
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

static int build_lib_path32(const char *prefix, const char *name, char *out,
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

static int elf32_collect_needed(const elf32_image_t *img, const char **out,
                                int max) {
  if (!img || !out || max <= 0 || !img->dyn_addr || !img->strtab)
    return 0;
  int count = 0;
  Elf32_Dyn *dyn = (Elf32_Dyn *)as_translate_ptr(img->as, img->dyn_addr);
  if (!dyn)
    return 0;
  uint32_t dyn_count = img->dyn_size / sizeof(Elf32_Dyn);
  for (uint32_t i = 0; i < dyn_count; i++) {
    uint32_t tag = (uint32_t)dyn[i].d_tag;
    if (tag == DT_NULL)
      break;
    if (tag != DT_NEEDED)
      continue;
    uint32_t off = dyn[i].d_un.d_val;
    if (off >= img->strsz)
      continue;
    const char *name =
        (const char *)as_translate_ptr(img->as, img->strtab + off);
    if (!name || !*name)
      continue;
    if (count < max)
      out[count++] = name;
  }
  return count;
}

static int elf32_is_loaded(elf32_image_t *images, int img_count,
                           const char *name) {
  if (!name)
    return 1;
  for (int i = 0; i < img_count; i++) {
    if (images[i].name && strcmp(images[i].name, name) == 0)
      return 1;
  }
  return 0;
}

static int elf32_find_library(const char *name, const char *base_dir,
                              const uint8_t **out_data, size_t *out_size,
                              int *out_owned) {
  if (!name || !out_data || !out_size)
    return -1;
  if (strchr(name, '/')) {
    *out_data = read_executable(name, out_size, out_owned);
    return *out_data ? 0 : -1;
  }

  const char *prefixes[] = {base_dir, "/system/lib", "/lib", "/usr/lib",
                            nullptr};
  char path[256];
  for (int i = 0; prefixes[i]; i++) {
    if (!prefixes[i] || !prefixes[i][0])
      continue;
    if (build_lib_path32(prefixes[i], name, path, sizeof(path)) < 0)
      continue;
    *out_data = read_executable(path, out_size, out_owned);
    if (*out_data)
      return 0;
  }
  return -1;
}

static int load_elf32_image(const uint8_t *file_data, size_t file_size,
                            address_space_t *as, uint32_t load_base,
                            elf32_image_t *out_img, const char *name) {
  if (!file_data || !as || !out_img)
    return -1;
  if (file_size < sizeof(Elf32_Ehdr))
    return -1;
  const Elf32_Ehdr *ehdr = (const Elf32_Ehdr *)file_data;

  Elf32_Phdr *phdr = (Elf32_Phdr *)(file_data + ehdr->e_phoff);
  uint32_t image_end = 0;
  for (int i = 0; i < ehdr->e_phnum; i++) {
    if (phdr[i].p_type != PT_LOAD)
      continue;
    if ((size_t)phdr[i].p_offset + (size_t)phdr[i].p_filesz > file_size)
      return -1;

    uint32_t seg_vaddr = load_base + phdr[i].p_vaddr;
    uint32_t start_page = seg_vaddr & PAGE_FRAME_MASK;
    uint32_t end_page =
        (seg_vaddr + phdr[i].p_memsz + PAGE_SIZE - 1) & PAGE_FRAME_MASK;

    for (uint32_t page = start_page; page < end_page; page += PAGE_SIZE) {
      uint32_t *pte = get_pte_for(as->page_directory, page, 0);
      if (!pte || !(*pte & PAGE_PRESENT)) {
        void *new_page = vmm_alloc_page();
        if (!new_page)
          return -1;
        uint32_t phys = vmm_virt_to_phys(new_page);
        if (!phys)
          return -1;
        vmm_map_page(as->page_directory, page, phys,
                     PAGE_PRESENT | PAGE_RW | PAGE_USER);
        memset(new_page, 0, PAGE_SIZE);
      }
    }

    for (uint32_t off = 0; off < phdr[i].p_filesz; off++) {
      uint32_t va = seg_vaddr + off;
      uint32_t page = va & PAGE_FRAME_MASK;
      uint32_t in_page = va & (PAGE_SIZE - 1);

      uint32_t *pte = get_pte_for(as->page_directory, page, 0);
      if (!pte || !(*pte & PAGE_PRESENT))
        return -1;

      uint32_t phys_page = *pte & PAGE_FRAME_MASK;
      uint8_t *dst =
          (uint8_t *)(uintptr_t)(phys_page + KERNEL_BASE + in_page);
      *dst = file_data[phdr[i].p_offset + off];
    }

    uint32_t seg_end = seg_vaddr + phdr[i].p_memsz;
    if (seg_end > image_end)
      image_end = seg_end;
  }

  memset(out_img, 0, sizeof(*out_img));
  out_img->name = name;
  out_img->as = as;
  out_img->load_base = load_base;
  out_img->entry = load_base + ehdr->e_entry;
  out_img->image_end = image_end;
  out_img->phdr = load_base + ehdr->e_phoff;
  out_img->phent = ehdr->e_phentsize;
  out_img->phnum = ehdr->e_phnum;
  if (elf32_parse_dynamic(out_img, ehdr, phdr) < 0)
    return -1;
  return 0;
}

static int execve_impl(const char *filename, char *const argv[],
                       char *const envp[], uint8_t abi) {
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
  int owned = 0;
  const uint8_t *file_data = read_executable(filename, &file_size, &owned);

  if (!file_data) {
    serial("[EXEC] Error: Could not read file '%s'\n", filename);
    terminal_printf("[EXEC] Error: Could not read file '%s'\n", filename);
    return -1;
  }

  /* Parse ELF Header */
  if (file_size < sizeof(Elf32_Ehdr)) {
    terminal_printf("[EXEC] Error: File too small for ELF header\n");
    if (owned)
      kfree((void *)file_data);
    return -1;
  }

  Elf32_Ehdr *ehdr = (Elf32_Ehdr *)file_data;
  if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' ||
      ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F') {
    terminal_printf("[EXEC] Error: Invalid ELF magic\n");
    if (owned)
      kfree((void *)file_data);
    return -1;
  }

  terminal_printf("[EXEC] ELF Loaded. Entry=0x%x, Segments=%d\n", ehdr->e_entry,
                  ehdr->e_phnum);

  /* Create a private address space for this user app so multiple .petal apps
     can coexist without overlapping at the same virtual addresses. */
  address_space_t *as = address_space_create();
  if (!as || !as->page_directory) {
    terminal_printf("[EXEC] Error: Could not create address space\n");
    kfree((void *)file_data);
    return -1;
  }

  uint32_t load_base = 0;
  if (ehdr->e_type == ET_DYN)
    load_base = ELF32_DYN_BASE;

  elf32_image_t images[16];
  elf32_image_t *image_ptrs[16];
  int img_count = 0;

  if (load_elf32_image(file_data, file_size, as, load_base, &images[0],
                       filename) < 0) {
    terminal_printf("[EXEC] Error: ELF load failed\n");
    address_space_destroy(as);
    if (owned)
      kfree((void *)file_data);
    return -1;
  }
  images[0].file_data = file_data;
  images[0].file_size = file_size;
  images[0].owned = owned;
  images[0].is_main = 1;
  image_ptrs[0] = &images[0];
  img_count = 1;

  char base_dir_buf[256];
  const char *base_dir = basename_dir32(filename, base_dir_buf,
                                        sizeof(base_dir_buf));

  for (int idx = 0; idx < img_count; idx++) {
    const char *needed[32];
    int needed_count = elf32_collect_needed(&images[idx], needed, 32);
    for (int i = 0; i < needed_count; i++) {
      const char *libname = needed[i];
      if (!libname || elf32_is_loaded(images, img_count, libname))
        continue;

      const uint8_t *lib_data = nullptr;
      size_t lib_size = 0;
      int lib_owned = 0;
      if (elf32_find_library(libname, base_dir, &lib_data, &lib_size,
                             &lib_owned) < 0) {
        terminal_printf("[EXEC] Error: Missing shared library '%s'\n",
                        libname);
        address_space_destroy(as);
        return -1;
      }

      if (img_count >= (int)(sizeof(images) / sizeof(images[0]))) {
        terminal_printf("[EXEC] Error: Too many shared libraries\n");
        address_space_destroy(as);
        return -1;
      }

      const Elf32_Ehdr *lib_eh = (const Elf32_Ehdr *)lib_data;
      uint32_t lib_base =
          (lib_eh && lib_eh->e_type == ET_DYN)
              ? (ELF32_DYN_BASE + 0x1000000u * (uint32_t)img_count)
              : 0;

      if (load_elf32_image(lib_data, lib_size, as, lib_base,
                           &images[img_count], libname) < 0) {
        terminal_printf("[EXEC] Error: Failed to load '%s'\n", libname);
        address_space_destroy(as);
        return -1;
      }
      images[img_count].file_data = lib_data;
      images[img_count].file_size = lib_size;
      images[img_count].owned = lib_owned;
      image_ptrs[img_count] = &images[img_count];
      img_count++;
    }
  }

  for (int i = 0; i < img_count; i++) {
    if (elf32_apply_relocations(&images[i], image_ptrs, img_count) < 0) {
      terminal_printf("[EXEC] Error: ELF relocation failed\n");
      address_space_destroy(as);
      return -1;
    }
  }

  uint32_t as_cr3 = vmm_virt_to_phys(as->page_directory);
  if (!as_cr3) {
    terminal_printf("[EXEC] Error: Could not resolve address-space CR3\n");
    address_space_destroy(as);
    if (owned)
      kfree((void *)file_data);
    return -1;
  }

  /* Cleanup buffer */
  void (*entry_point)(void) =
      (void (*)(void))(uintptr_t)(images[0].entry);
  for (int i = 0; i < img_count; i++) {
    if (images[i].owned)
      kfree((void *)images[i].file_data);
  }

  /* Create background task */
  serial("[EXEC] Spawning task for %s at 0x%x\n", filename, entry_point);
  task_t *t = task_create(entry_point, 0);
  if (t) {
    t->is_user_app = 1;
    t->abi = abi;
    t->cr3 = as_cr3;
    t->launch_arg[0] = 0;
    const char *base = basename_ptr(filename);
    strncpy(t->name, base, sizeof(t->name) - 1);
    t->name[sizeof(t->name) - 1] = 0;
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

static int execve64_impl(const char *filename, char *const argv[],
                         uint8_t abi) {
  (void)argv;
  (void)abi;
  size_t file_size = 0;
  int owned = 0;
  const uint8_t *file_data = read_executable(filename, &file_size, &owned);

  if (!file_data) {
    terminal_printf("[EXEC64] Error: Could not read file '%s'\n", filename);
    return -1;
  }

  if (!elf64_is_valid(file_data, (uint32_t)file_size)) {
    terminal_printf("[EXEC64] Error: Invalid ELF64 magic\n");
    if (owned)
      kfree((void *)file_data);
    return -1;
  }

  elf64_load_info_t info;
  int r = elf64_load_from_buffer((void *)file_data, (uint32_t)file_size, &info);
  if (r < 0) {
    terminal_printf("[EXEC64] Error: ELF64 parse failed (%d)\n", r);
    if (owned)
      kfree((void *)file_data);
    return -1;
  }

  if (!cpu_is_long_mode()) {
    terminal_printf("[EXEC64] Error: CPU not in long mode. Boot 64-bit kernel.\n");
    elf64_unload_kernel_space(&info);
    if (owned)
      kfree((void *)file_data);
    return -1;
  }

  terminal_printf("[EXEC64] ELF64 parsed (entry=0x%llx). Loader not wired.\n",
                  info.entry_point);
  elf64_unload_kernel_space(&info);
  if (owned)
    kfree((void *)file_data);
  return -1;
}

static int detect_linux_abi(const uint8_t *file_data, size_t file_size) {
  if (!file_data || file_size < 5)
    return -1;
  if (file_data[0] != 0x7F || file_data[1] != 'E' || file_data[2] != 'L' ||
      file_data[3] != 'F')
    return -1;

  uint8_t cls = file_data[4];
  if (cls == ELFCLASS32) {
    if (file_size < sizeof(Elf32_Ehdr))
      return -1;
    const Elf32_Ehdr *eh = (const Elf32_Ehdr *)file_data;
    if (eh->e_machine == EM_386)
      return TASK_ABI_LINUX_I386;
    if (eh->e_machine == EM_X86_64)
      return TASK_ABI_LINUX_X32;
    return -1;
  }

  if (cls == ELFCLASS64) {
    if (file_size < sizeof(elf64_ehdr_t))
      return -1;
    const elf64_ehdr_t *eh = (const elf64_ehdr_t *)file_data;
    if (eh->e_machine == EM_X86_64)
      return TASK_ABI_LINUX_X86_64;
    return -1;
  }

  return -1;
}

extern "C" int execve(const char *filename, char *const argv[],
                      char *const envp[]) {
  return execve_impl(filename, argv, envp, TASK_ABI_CHRYSALIS);
}

extern "C" int execve_linux_i386(const char *filename, char *const argv[]) {
  return execve_impl(filename, argv, nullptr, TASK_ABI_LINUX_I386);
}

extern "C" int execve_linux_x86_64_full(const char *filename,
                                        char *const argv[]);

extern "C" int execve_linux_x86_64(const char *filename, char *const argv[]) {
  if (cpu_is_long_mode()) {
    return execve_linux_x86_64_full(filename, argv);
  }
  return execve64_impl(filename, argv, TASK_ABI_LINUX_X86_64);
}

static int execve_linux_x32(const char *filename, char *const argv[]) {
  (void)filename;
  (void)argv;
  terminal_printf("[EXEC] Error: Linux x32 ABI not supported yet\n");
  return -1;
}

extern "C" int execve_linux_auto(const char *filename, char *const argv[]) {
  size_t file_size = 0;
  int owned = 0;
  const uint8_t *file_data = read_executable(filename, &file_size, &owned);
  if (!file_data) {
    terminal_printf("[EXEC] Error: Could not read file '%s'\n", filename);
    return -1;
  }

  int abi = detect_linux_abi(file_data, file_size);
  if (owned)
    kfree((void *)file_data);

  if (abi == TASK_ABI_LINUX_I386)
    return execve_linux_i386(filename, argv);
  if (abi == TASK_ABI_LINUX_X86_64)
    return execve_linux_x86_64(filename, argv);
  if (abi == TASK_ABI_LINUX_X32)
    return execve_linux_x32(filename, argv);

  terminal_printf("[EXEC] Error: Unsupported Linux ELF ABI\n");
  return -1;
}

extern "C" int exec_from_path(const char *path, char *const argv[]) {
  return execve(path, argv, nullptr);
}

extern "C" int exec_from_path_linux_i386(const char *path,
                                         char *const argv[]) {
  return execve_linux_i386(path, argv);
}

extern "C" int exec_from_path_linux_x86_64(const char *path,
                                           char *const argv[]) {
  return execve_linux_x86_64(path, argv);
}

extern "C" int exec_from_path_linux_auto(const char *path,
                                         char *const argv[]) {
  return execve_linux_auto(path, argv);
}
