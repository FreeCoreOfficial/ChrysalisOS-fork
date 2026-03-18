#include "elf64.h"
#include <stdint.h>
#include <stddef.h>

extern void* kmalloc(uint32_t size);
extern void kfree(void* ptr);
extern void* memcpy(void* dst, const void* src, uint32_t n);
extern void* memset(void* dst, int c, uint32_t n);
/* terminal_printf not available in 64-bit kernel yet */

int elf64_is_valid(const void* data, uint32_t size) {
    if (!data || size < sizeof(elf64_ehdr_t)) return 0;
    const unsigned char* id = (const unsigned char*)data;
    if (id[0] != 0x7f || id[1] != 'E' || id[2] != 'L' || id[3] != 'F') return 0;
    if (id[4] != ELFCLASS64) return 0;
    if (id[5] != ELFDATA2LSB) return 0;
    return 1;
}

int elf64_parse(const void* data, uint32_t size, elf64_load_info_t* out) {
    if (!elf64_is_valid(data, size)) return -1;
    if (!out) return -2;

    if (size < sizeof(elf64_ehdr_t)) return -3;
    const elf64_ehdr_t* eh = (const elf64_ehdr_t*)data;
    out->ehdr = *eh;
    out->entry_point = eh->e_entry;
    out->seg_count = 0;

    if (eh->e_machine != EM_X86_64) {
        return -4;
    }

    uint64_t phoff = eh->e_phoff;
    uint16_t phentsize = eh->e_phentsize;
    uint16_t phnum = eh->e_phnum;

    if (!phentsize || !phnum) return -5;

    for (uint16_t i = 0; i < phnum; ++i) {
        uint64_t entry_off = phoff + (uint64_t)i * (uint64_t)phentsize;
        if (entry_off + sizeof(elf64_phdr_t) > size) return -6;
        const elf64_phdr_t* ph = (const elf64_phdr_t*)((const uint8_t*)data + (uint32_t)entry_off);
        if (ph->p_type == PT_LOAD) {
            if (out->seg_count >= ELF64_MAX_LOAD_SEGMENTS) {
                return -7;
            }
            elf64_segment_t* seg = &out->segments[out->seg_count++];
            seg->kernel_buf = NULL;
            seg->vaddr = ph->p_vaddr;
            seg->filesz = ph->p_filesz;
            seg->memsz = ph->p_memsz;
            seg->flags = ph->p_flags;
            if (ph->p_offset + ph->p_filesz > size) {
                return -8;
            }
        }
    }

    return 0;
}

int elf64_load_into_kernel_space(const void* data, uint32_t size, elf64_load_info_t* out) {
    if (!data || !out) return -1;
    int r = elf64_parse(data, size, out);
    if (r < 0) return r;

    for (int i = 0; i < out->seg_count; ++i) {
        elf64_segment_t* s = &out->segments[i];
        uint64_t need = s->filesz;
        if (need > 0x7FFFFFFFu) {
            for (int j = 0; j < i; ++j) {
                if (out->segments[j].kernel_buf) kfree(out->segments[j].kernel_buf);
                out->segments[j].kernel_buf = NULL;
            }
            return -2;
        }
        void* buf = kmalloc((need > 0) ? (uint32_t)need : 1);
        if (!buf) {
            for (int j = 0; j < i; ++j) {
                if (out->segments[j].kernel_buf) kfree(out->segments[j].kernel_buf);
                out->segments[j].kernel_buf = NULL;
            }
            return -3;
        }

        if (s->filesz > 0) {
            const elf64_ehdr_t* eh = (const elf64_ehdr_t*)data;
            uint64_t phoff = eh->e_phoff;
            uint16_t phentsize = eh->e_phentsize;
            uint16_t phnum = eh->e_phnum;
            uint64_t found_offset = 0;
            int matched = 0;
            for (uint16_t p = 0; p < phnum; ++p) {
                uint64_t off = phoff + (uint64_t)p * (uint64_t)phentsize;
                const elf64_phdr_t* ph = (const elf64_phdr_t*)((const uint8_t*)data + (uint32_t)off);
                if (ph->p_type == PT_LOAD && ph->p_vaddr == s->vaddr && ph->p_filesz == s->filesz && ph->p_memsz == s->memsz) {
                    found_offset = ph->p_offset;
                    matched = 1;
                    break;
                }
            }
            if (!matched) {
                kfree(buf);
                for (int j = 0; j < i; ++j) {
                    if (out->segments[j].kernel_buf) kfree(out->segments[j].kernel_buf);
                    out->segments[j].kernel_buf = NULL;
                }
                return -4;
            }
            const void* srcptr = (const uint8_t*)data + (uint32_t)found_offset;
            memcpy(buf, srcptr, (uint32_t)s->filesz);
        } else {
            memset(buf, 0, 1);
        }
        s->kernel_buf = buf;
    }

    return 0;
}

void elf64_unload_kernel_space(elf64_load_info_t* info) {
    if (!info) return;
    for (int i = 0; i < info->seg_count; ++i) {
        if (info->segments[i].kernel_buf) {
            kfree(info->segments[i].kernel_buf);
            info->segments[i].kernel_buf = NULL;
        }
    }
    info->seg_count = 0;
}

int elf64_load_from_buffer(void* buf, uint32_t len, elf64_load_info_t* out) {
    return elf64_load_into_kernel_space(buf, len, out);
}
