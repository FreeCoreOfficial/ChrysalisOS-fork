#include "pmm64.h"
#include "../../smp/multiboot.h"

#define PMM64_MAX_RANGES 128
#define PMM64_PAGE_SIZE 0x1000ULL
#define PMM64_MAX_PHYS  0x100000000ULL /* 4 GiB mapped */

typedef struct {
    uint64_t start;
    uint64_t end;
} pmm64_range_t;

static pmm64_range_t ranges[PMM64_MAX_RANGES];
static int range_count = 0;
static int pmm_ready = 0;

static uint64_t align_up(uint64_t v) {
    return (v + (PMM64_PAGE_SIZE - 1)) & ~(PMM64_PAGE_SIZE - 1);
}

static uint64_t align_down(uint64_t v) {
    return v & ~(PMM64_PAGE_SIZE - 1);
}

int pmm64_is_ready(void) {
    return pmm_ready;
}

void pmm64_init(uint64_t mb_info_addr, uint64_t reserved_end) {
    range_count = 0;
    pmm_ready = 0;
    if (mb_info_addr == 0)
        return;

    uint64_t min_start = 0x100000ULL;
    if (reserved_end < min_start)
        reserved_end = min_start;
    reserved_end = align_up(reserved_end);

    struct multiboot2_tag *tag =
        (struct multiboot2_tag *)(uintptr_t)(mb_info_addr + 8);

    while (tag->type != MULTIBOOT2_TAG_TYPE_END) {
        if (tag->type == MULTIBOOT2_TAG_TYPE_MMAP) {
            struct multiboot2_tag_mmap *mmap =
                (struct multiboot2_tag_mmap *)tag;
            for (struct multiboot2_mmap_entry *entry = mmap->entries;
                 (uint8_t *)entry < (uint8_t *)mmap + mmap->common.size;
                 entry = (struct multiboot2_mmap_entry *)((uint8_t *)entry +
                                                          mmap->entry_size)) {
                if (entry->type != MULTIBOOT2_MEMORY_AVAILABLE)
                    continue;

                uint64_t start = entry->addr;
                uint64_t end = entry->addr + entry->len;
                if (end <= reserved_end)
                    continue;
                if (start < reserved_end)
                    start = reserved_end;
                if (start < min_start)
                    start = min_start;

                start = align_up(start);
                end = align_down(end);
                if (start >= end)
                    continue;
                if (start >= PMM64_MAX_PHYS)
                    continue;
                if (end > PMM64_MAX_PHYS)
                    end = PMM64_MAX_PHYS;

                if (range_count < PMM64_MAX_RANGES) {
                    ranges[range_count].start = start;
                    ranges[range_count].end = end;
                    range_count++;
                }
            }
        }
        tag = (struct multiboot2_tag *)((uint8_t *)tag + ((tag->size + 7) & ~7));
    }

    if (range_count > 0)
        pmm_ready = 1;
}

uint64_t pmm64_alloc_frame(void) {
    if (!pmm_ready)
        return 0;
    for (int i = 0; i < range_count; i++) {
        uint64_t start = ranges[i].start;
        if (start + PMM64_PAGE_SIZE <= ranges[i].end) {
            ranges[i].start = start + PMM64_PAGE_SIZE;
            return start;
        }
    }
    return 0;
}
