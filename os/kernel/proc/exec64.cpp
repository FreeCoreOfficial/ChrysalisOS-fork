#include "exec64.h"
#include "../elf/elf64.h"
#include "../arch/x86_64/paging64.h"
#include "../arch/x86_64/user64.h"
#include "../string.h"
#include "../mem/kmalloc.h"
#include "../mem/user64_vm.h"
#include "../sched/task64.h"
#include "../fs/fs.h"

extern "C" void *kmalloc(size_t size);
extern "C" void kfree(void *ptr);
extern "C" const void *ramfs_read_file(const char *name, size_t *out_size);

#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PT_PHDR 6

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

#define AT_NULL 0
#define AT_PHDR 3
#define AT_PHENT 4
#define AT_PHNUM 5
#define AT_PAGESZ 6
#define AT_BASE 7
#define AT_ENTRY 9

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

  return nullptr;
}

static int read_exec64_path(const char *path, const uint8_t **out_data,
                            size_t *out_size, int *out_owned) {
  if (!out_data || !out_size)
    return -1;
  *out_data = read_exec64(path, out_size, out_owned);
  return (*out_data) ? 0 : -1;
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

static uint32_t elf64_sym_count_from_hash(uint64_t hash_addr) {
  if (!hash_addr)
    return 0;
  uint32_t *hash = (uint32_t *)(uintptr_t)hash_addr;
  if (!hash)
    return 0;
  return hash[1];
}

static uint32_t elf64_sym_count_from_gnu_hash(uint64_t gnu_hash_addr) {
  if (!gnu_hash_addr)
    return 0;
  uint32_t *hdr = (uint32_t *)(uintptr_t)gnu_hash_addr;
  if (!hdr)
    return 0;
  uint32_t nbuckets = hdr[0];
  uint32_t symoffset = hdr[1];
  uint32_t bloom_size = hdr[2];
  if (nbuckets == 0)
    return 0;

  uint64_t *bloom = (uint64_t *)(uintptr_t)(gnu_hash_addr + 16);
  uint32_t *buckets = (uint32_t *)(uintptr_t)(bloom + bloom_size);
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
  uint64_t count = (img->strtab - img->symtab) / img->syment;
  if (count > 0xFFFFFFFFu)
    count = 0xFFFFFFFFu;
  return (uint32_t)count;
}

static int elf64_parse_dynamic(elf64_image_t *img, const elf64_ehdr_t *ehdr,
                               const elf64_phdr_t *phdr, size_t phnum) {
  if (!img || !ehdr || !phdr)
    return -1;
  const elf64_phdr_t *dyn_ph = nullptr;
  for (uint16_t i = 0; i < phnum; i++) {
    if (phdr[i].p_type == PT_DYNAMIC) {
      dyn_ph = &phdr[i];
      break;
    }
  }

  if (!dyn_ph)
    return 0;

  img->dyn_addr = img->load_base + dyn_ph->p_vaddr;
  img->dyn_size = dyn_ph->p_memsz;

  const Elf64_Dyn *dyn = (const Elf64_Dyn *)(uintptr_t)img->dyn_addr;
  uint64_t dyn_count = img->dyn_size / sizeof(Elf64_Dyn);

  for (uint64_t i = 0; i < dyn_count; i++) {
    uint64_t tag = (uint64_t)dyn[i].d_tag;
    uint64_t val = dyn[i].d_un.d_val;
    uint64_t ptr = dyn[i].d_un.d_ptr;
    switch (tag) {
    case DT_NULL:
      i = dyn_count;
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
    img->syment = sizeof(Elf64_Sym);
  if (!img->rela_ent)
    img->rela_ent = sizeof(Elf64_Rela);
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
                                uint64_t *out_addr) {
  if (!img || !out_addr)
    return -1;
  const Elf64_Sym *sym = elf64_get_sym(img, sym_index);
  if (!sym)
    return -1;
  if (sym->st_shndx != 0) {
    *out_addr = img->load_base + sym->st_value;
    return 0;
  }

  const char *name = elf64_sym_name(img, sym);
  if (!name)
    return -1;

  for (int i = 0; i < img_count; i++) {
    elf64_image_t *other = images[i];
    if (!other || !other->symtab)
      continue;
    uint32_t count = other->sym_count;
    if (!count)
      count = elf64_sym_count_fallback(other);
    for (uint32_t si = 0; si < count; si++) {
      const Elf64_Sym *osym = elf64_get_sym(other, si);
      if (!osym || osym->st_shndx == 0)
        continue;
      const char *oname = elf64_sym_name(other, osym);
      if (!oname)
        continue;
      if (strcmp(oname, name) == 0) {
        *out_addr = other->load_base + osym->st_value;
        return 0;
      }
    }
  }

  uint8_t bind = ELF64_ST_BIND(sym->st_info);
  if (bind == STB_WEAK) {
    *out_addr = 0;
    return 0;
  }
  return -1;
}

static int elf64_apply_relocs(elf64_image_t *img, elf64_image_t **images,
                              int img_count) {
  if (!img)
    return -1;
  if (!img->rela_ent)
    img->rela_ent = sizeof(Elf64_Rela);

  auto apply_rela = [&](uint64_t base, uint64_t size, uint64_t ent) -> int {
    if (!base || size == 0)
      return 0;
    uint64_t count = size / ent;
    for (uint64_t i = 0; i < count; i++) {
      uint64_t addr = base + i * ent;
      Elf64_Rela *rela = (Elf64_Rela *)(uintptr_t)addr;
      if (!rela)
        return -1;
      uint64_t reloc_va = img->load_base + rela->r_offset;
      uint64_t *where = (uint64_t *)(uintptr_t)reloc_va;
      if (!where)
        return -1;
      uint32_t type = ELF64_R_TYPE(rela->r_info);
      uint32_t symi = ELF64_R_SYM(rela->r_info);
      if (type == R_X86_64_RELATIVE) {
        *where = img->load_base + (uint64_t)rela->r_addend;
        continue;
      }
      uint64_t sym_addr = 0;
      if (elf64_resolve_symbol(img, symi, images, img_count, &sym_addr) < 0)
        return -1;
      switch (type) {
      case R_X86_64_64:
      case R_X86_64_GLOB_DAT:
      case R_X86_64_JUMP_SLOT:
        *where = sym_addr + (uint64_t)rela->r_addend;
        break;
      default:
        return -1;
      }
    }
    return 0;
  };

  if (apply_rela(img->rela_addr, img->rela_sz, img->rela_ent) < 0)
    return -1;
  if (img->pltrel_sz > 0 && img->jmprel_addr) {
    if (img->pltrel_type != DT_RELA)
      return -1;
    if (apply_rela(img->jmprel_addr, img->pltrel_sz, img->rela_ent) < 0)
      return -1;
  }
  return 0;
}

static int load_elf64_image(const uint8_t *file_data, size_t file_size,
                            uint64_t load_base, elf64_image_t *out_img,
                            char *interp_buf, size_t interp_buf_sz) {
  if (!file_data || file_size < sizeof(elf64_ehdr_t))
    return -1;

  const elf64_ehdr_t *eh = (const elf64_ehdr_t *)file_data;
  if (!elf64_is_valid(file_data, (uint32_t)file_size))
    return -1;
  if (eh->e_machine != EM_X86_64)
    return -1;
  if (eh->e_type != ET_EXEC && eh->e_type != ET_DYN)
    return -1;

  const elf64_phdr_t *phdr =
      (const elf64_phdr_t *)(file_data + eh->e_phoff);
  uint64_t image_end = 0;
  uint64_t phdr_addr = 0;
  uint64_t phent = eh->e_phentsize;
  uint64_t phnum = eh->e_phnum;

  for (uint16_t i = 0; i < eh->e_phnum; i++) {
    if (phdr[i].p_type == PT_PHDR)
      phdr_addr = load_base + phdr[i].p_vaddr;
    if (phdr[i].p_type == PT_INTERP && interp_buf && interp_buf_sz > 0) {
      if (phdr[i].p_offset + phdr[i].p_filesz <= file_size) {
        size_t len = (size_t)phdr[i].p_filesz;
        if (len >= interp_buf_sz)
          len = interp_buf_sz - 1;
        memcpy(interp_buf, file_data + phdr[i].p_offset, len);
        interp_buf[len] = 0;
      }
    }
    if (phdr[i].p_type != PT_LOAD)
      continue;
    if (phdr[i].p_offset + phdr[i].p_filesz > file_size)
      return -1;

    uint64_t seg_vaddr = load_base + phdr[i].p_vaddr;
    if (map_user_segment(seg_vaddr, phdr[i].p_memsz, phdr[i].p_flags) < 0)
      return -1;

    if (phdr[i].p_filesz > 0) {
      memcpy((void *)(uintptr_t)seg_vaddr, file_data + phdr[i].p_offset,
             (size_t)phdr[i].p_filesz);
    }

    uint64_t end = seg_vaddr + phdr[i].p_memsz;
    if (end > image_end)
      image_end = end;
  }

  if (!phdr_addr)
    phdr_addr = load_base + eh->e_phoff;

  if (out_img) {
    memset(out_img, 0, sizeof(*out_img));
    out_img->file_data = file_data;
    out_img->file_size = file_size;
    out_img->load_base = load_base;
    out_img->entry = load_base + eh->e_entry;
    out_img->image_end = image_end;
    out_img->phdr = phdr_addr;
    out_img->phent = phent;
    out_img->phnum = phnum;
    out_img->is_main = 0;
    out_img->sym_count = 0;
    if (elf64_parse_dynamic(out_img, eh, phdr, eh->e_phnum) < 0)
      return -1;
  }
  return 0;
}

static uint64_t build_user_stack(char *const argv[], uint64_t stack_top,
                                 uint64_t at_entry, uint64_t at_phdr,
                                 uint64_t at_phent, uint64_t at_phnum,
                                 uint64_t at_base) {
  uint64_t sp = stack_top;

  int argc = 0;
  while (argv && argv[argc])
    argc++;

  const char *envp[] = {nullptr};
  int envc = 0;

  uint64_t arg_ptrs[64];
  uint64_t env_ptrs[8];

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

  sp &= ~0xFULL;

  auto push64 = [&](uint64_t v) {
    sp -= 8;
    *(uint64_t *)(uintptr_t)sp = v;
  };

  push64(0);
  push64(0);
  push64(AT_NULL);

  push64(at_entry);
  push64(AT_ENTRY);
  push64(at_phnum);
  push64(AT_PHNUM);
  push64(at_phent);
  push64(AT_PHENT);
  push64(at_phdr);
  push64(AT_PHDR);
  push64(0x1000);
  push64(AT_PAGESZ);
  push64(at_base);
  push64(AT_BASE);

  push64(0);
  for (int i = envc - 1; i >= 0; i--)
    push64(env_ptrs[i]);

  push64(0);
  for (int i = argc - 1; i >= 0; i--)
    push64(arg_ptrs[i]);

  push64((uint64_t)argc);
  return sp;
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

  const char *prefixes[] = {base_dir, "/system/lib", "/lib", "/usr/lib",
                            nullptr};
  char path[256];
  for (int i = 0; prefixes[i]; i++) {
    if (!prefixes[i] || !prefixes[i][0])
      continue;
    if (build_lib_path(prefixes[i], name, path, sizeof(path)) < 0)
      continue;
    if (read_exec64_path(path, out_data, out_size, out_owned) == 0)
      return 0;
  }
  return -1;
}

static int exec64_from_buffer(const uint8_t *file_data, size_t file_size,
                              char *const argv[], const char *image_path) {
  elf64_image_t images[16];
  elf64_image_t *image_ptrs[16];
  int img_count = 0;

  char interp_path[256];
  interp_path[0] = 0;

  const elf64_ehdr_t *eh = (const elf64_ehdr_t *)file_data;
  uint64_t main_base = (eh && eh->e_type == ET_DYN) ? ELF64_DYN_BASE : 0;

  if (load_elf64_image(file_data, file_size, main_base, &images[0],
                       interp_path, sizeof(interp_path)) < 0)
    return -1;
  images[0].name = image_path ? image_path : "app";
  images[0].is_main = 1;
  image_ptrs[0] = &images[0];
  img_count = 1;

  char base_dir_buf[256];
  const char *base_dir = basename_dir(image_path, base_dir_buf,
                                      sizeof(base_dir_buf));

  int interp_index = -1;
  if (interp_path[0]) {
    const uint8_t *interp_data = nullptr;
    size_t interp_size = 0;
    int interp_owned = 0;
    if (read_exec64_path(interp_path, &interp_data, &interp_size,
                         &interp_owned) < 0)
      return -1;

    if (img_count >= (int)(sizeof(images) / sizeof(images[0])))
      return -1;

    uint64_t interp_base = ELF64_INTERP_BASE;
    if (load_elf64_image(interp_data, interp_size, interp_base,
                         &images[img_count], nullptr, 0) < 0)
      return -1;
    images[img_count].name = interp_path;
    images[img_count].owned = interp_owned;
    image_ptrs[img_count] = &images[img_count];
    interp_index = img_count;
    img_count++;
  }

  for (int idx = 0; idx < img_count; idx++) {
    const char *needed[32];
    int needed_count = elf64_collect_needed(&images[idx], needed, 32);
    for (int i = 0; i < needed_count; i++) {
      const char *libname = needed[i];
      if (!libname || elf64_is_loaded(images, img_count, libname))
        continue;

      const uint8_t *lib_data = nullptr;
      size_t lib_size = 0;
      int owned = 0;
      if (elf64_find_library(libname, base_dir, &lib_data, &lib_size, &owned) <
          0)
        return -1;

      if (img_count >= (int)(sizeof(images) / sizeof(images[0])))
        return -1;

      const elf64_ehdr_t *lib_eh = (const elf64_ehdr_t *)lib_data;
      uint64_t lib_base =
          (lib_eh && lib_eh->e_type == ET_DYN)
              ? (ELF64_DYN_BASE + 0x1000000ULL * (uint64_t)img_count)
              : 0;

      if (load_elf64_image(lib_data, lib_size, lib_base, &images[img_count],
                           nullptr, 0) < 0)
        return -1;
      images[img_count].name = libname;
      images[img_count].owned = owned;
      image_ptrs[img_count] = &images[img_count];
      img_count++;
    }
  }

  for (int i = 0; i < img_count; i++) {
    if (elf64_apply_relocs(&images[i], image_ptrs, img_count) < 0)
      return -1;
  }

  uint64_t image_end = 0;
  for (int i = 0; i < img_count; i++) {
    if (images[i].image_end > image_end)
      image_end = images[i].image_end;
  }

  uint64_t stack_top = ELF64_STACK_TOP;
  if (map_user_stack(stack_top, ELF64_STACK_SIZE) < 0)
    return -1;

  uint64_t at_base = 0;
  uint64_t entry = images[0].entry;
  if (interp_index >= 0) {
    at_base = images[interp_index].load_base;
    entry = images[interp_index].entry;
  }

  uint64_t rsp = build_user_stack(argv ? argv : (char *const *)"", stack_top,
                                  images[0].entry, images[0].phdr,
                                  images[0].phent, images[0].phnum, at_base);

  user64_init_process(image_end);
  task64_set_user_stack(rsp);

  struct user64_context ctx;
  ctx.rip = entry;
  ctx.rsp = rsp;
  ctx.rflags = 0x202;
  user64_enter(&ctx);
  return 0;
}

int exec64_from_module(void *start, uint64_t size) {
  char *argv[] = {(char *)"module", nullptr};
  return exec64_from_buffer((const uint8_t *)start, (size_t)size, argv,
                            nullptr);
}

int execve_linux_x86_64_full(const char *filename, char *const argv[]) {
  size_t file_size = 0;
  int owned = 0;
  const uint8_t *file_data = read_exec64(filename, &file_size, &owned);
  if (!file_data)
    return -1;
  int r = exec64_from_buffer(file_data, file_size, argv, filename);
  if (owned)
    kfree((void *)file_data);
  return r;
}
