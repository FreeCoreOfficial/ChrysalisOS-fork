#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EI_NIDENT 16

#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define EV_CURRENT 1

#define ET_EXEC 2
#define EM_X86_64 62

#define PT_NULL 0
#define PT_LOAD 1

#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elf64_ehdr_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} elf64_phdr_t;

#define ELF64_MAX_LOAD_SEGMENTS 8

typedef struct {
    void* kernel_buf;
    uint64_t vaddr;
    uint64_t filesz;
    uint64_t memsz;
    uint32_t flags;
} elf64_segment_t;

typedef struct {
    elf64_ehdr_t ehdr;
    elf64_segment_t segments[ELF64_MAX_LOAD_SEGMENTS];
    int seg_count;
    uint64_t entry_point;
} elf64_load_info_t;

int elf64_is_valid(const void* data, uint32_t size);
int elf64_parse(const void* data, uint32_t size, elf64_load_info_t* out);
int elf64_load_into_kernel_space(const void* data, uint32_t size, elf64_load_info_t* out);
void elf64_unload_kernel_space(elf64_load_info_t* info);
int elf64_load_from_buffer(void* buf, uint32_t len, elf64_load_info_t* out);

#ifdef __cplusplus
}
#endif
