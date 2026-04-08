/* kernel/video/gpu_bochs.c */
#include "gpu_bochs.h"
#include "gpu.h"
#include "../drivers/serial.h"
#ifndef __x86_64__
#include "../mm/vmm.h"
#include "../memory/pmm.h"
#include "../mm/paging.h"
#else
#include "../arch/x86_64/paging64.h"
#endif
#include "../arch/i386/io.h"

/* Bochs VBE Extensions (BGA) Definitions */
#define VBE_DISPI_IOPORT_INDEX      0x01CE
#define VBE_DISPI_IOPORT_DATA       0x01CF

#define VBE_DISPI_INDEX_ID          0x0
#define VBE_DISPI_INDEX_XRES        0x1
#define VBE_DISPI_INDEX_YRES        0x2
#define VBE_DISPI_INDEX_BPP         0x3
#define VBE_DISPI_INDEX_ENABLE      0x4
#define VBE_DISPI_INDEX_BANK        0x5
#define VBE_DISPI_INDEX_VIRT_WIDTH  0x6
#define VBE_DISPI_INDEX_VIRT_HEIGHT 0x7
#define VBE_DISPI_INDEX_X_OFFSET    0x8
#define VBE_DISPI_INDEX_Y_OFFSET    0x9

#define VBE_DISPI_DISABLED          0x00
#define VBE_DISPI_ENABLED           0x01
#define VBE_DISPI_LFB_ENABLED       0x40
#define VBE_DISPI_NOCLEARMEM        0x80

#define BOCHS_VENDOR_ID             0x1234
#define BOCHS_DEVICE_ID             0x1111

#ifndef __x86_64__
#define BOCHS_LFB_VIRT_BASE         0xE2000000
#else
#define BOCHS_LFB_VIRT_BASE         0xFFFFFFFFC0000000ULL
#endif

/* PCI Configuration Ports */
#define PCI_CONFIG_ADDRESS          0xCF8
#define PCI_CONFIG_DATA             0xCFC

/* Import serial logging */
extern void serial(const char *fmt, ...);

/* Driver State */
static gpu_device_t bochs_device = {0};

/* --- PCI Helper Functions (Local implementation to avoid dependency on disabled pci.cpp) --- */

static uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

/* --- Bochs VBE Register Helpers --- */

static void bochs_write(uint16_t index, uint16_t value) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    outw(VBE_DISPI_IOPORT_DATA, value);
}

static uint16_t bochs_read(uint16_t index) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    return inw(VBE_DISPI_IOPORT_DATA);
}

/* --- GPU Operations Implementation --- */

static void bochs_putpixel(gpu_device_t* dev, uint32_t x, uint32_t y, uint32_t color) {
    if (!dev->virt_addr || x >= dev->width || y >= dev->height) return;

    /* Assuming 32 BPP for now as enforced by set_mode */
    uint32_t* buffer = (uint32_t*)dev->virt_addr;
    buffer[y * (dev->pitch / 4) + x] = color;
}

static void bochs_fillrect(gpu_device_t* dev, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!dev->virt_addr) return;

    /* Clipping */
    if (x >= dev->width || y >= dev->height) return;
    if (x + w > dev->width) w = dev->width - x;
    if (y + h > dev->height) h = dev->height - y;

    uint32_t* buffer = (uint32_t*)dev->virt_addr;
    uint32_t pitch32 = dev->pitch / 4;

    for (uint32_t j = 0; j < h; j++) {
        uint32_t offset = (y + j) * pitch32 + x;
        for (uint32_t i = 0; i < w; i++) {
            buffer[offset + i] = color;
        }
    }
}

static void bochs_clear(gpu_device_t* dev, uint32_t color) {
    if (!dev->virt_addr) return;

    uint32_t* buffer = (uint32_t*)dev->virt_addr;
    uint32_t count = (dev->pitch / 4) * dev->height;
    
    for (uint32_t i = 0; i < count; i++) {
        buffer[i] = color;
    }
}

static void bochs_flush(gpu_device_t* dev) {
    (void)dev;
    /* Memory barrier */
    asm volatile("sfence" ::: "memory");
}

static int bochs_set_mode(gpu_device_t* dev, uint32_t width, uint32_t height, uint32_t bpp) {
    serial("[GPU] Bochs set_mode: %dx%dx%d\n", width, height, bpp);

    if (bpp != 32) {
        serial("[GPU] Bochs driver currently only supports 32 BPP.\n");
        return -1;
    }

    /* Disable VBE */
    bochs_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);

    /* Set X, Y, BPP */
    bochs_write(VBE_DISPI_INDEX_XRES, (uint16_t)width);
    bochs_write(VBE_DISPI_INDEX_YRES, (uint16_t)height);
    bochs_write(VBE_DISPI_INDEX_BPP,  (uint16_t)bpp);

    /* Re-enable VBE with Linear Framebuffer (LFB) */
    bochs_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);

    /* Update device structure */
    dev->width = width;
    dev->height = height;
    dev->bpp = bpp;
    dev->pitch = width * (bpp / 8); /* Bochs usually packs lines tightly */

    /* Verify if settings stuck */
    uint16_t new_x = bochs_read(VBE_DISPI_INDEX_XRES);
    uint16_t new_y = bochs_read(VBE_DISPI_INDEX_YRES);
    
    if (new_x != width || new_y != height) {
        serial("[GPU] Bochs failed to set resolution (read back %dx%d)\n", new_x, new_y);
        return -1;
    }

    /* Clear screen after mode switch */
    bochs_clear(dev, 0x000000);

    return 0;
}

static gpu_ops_t bochs_ops = {
    .set_mode = bochs_set_mode,
    .putpixel = bochs_putpixel,
    .fillrect = bochs_fillrect,
    .clear    = bochs_clear,
    .flush    = bochs_flush
};

/* --- Initialization --- */

void gpu_bochs_init(void) {
    serial("[GPU] Probing PCI for Bochs/QEMU GPU...\n");

    /* uint32_t pci_addr = 0; */
    uint32_t bar0 = 0;
    int found = 0;

    /* Brute-force PCI scan for Vendor 0x1234, Device 0x1111 */
    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint32_t slot = 0; slot < 32; slot++) {
            for (uint32_t func = 0; func < 8; func++) {
                /* Read Vendor ID (Offset 0x00, low word) */
                uint32_t id_reg = pci_read_config(bus, slot, func, 0x00);
                uint16_t vendor = id_reg & 0xFFFF;
                uint16_t device = (id_reg >> 16) & 0xFFFF;

                if (vendor == 0xFFFF) continue; /* Invalid device */

                if (vendor == BOCHS_VENDOR_ID && device == BOCHS_DEVICE_ID) {
                    serial("[GPU] Bochs GPU detected (PCI %04x:%04x) at %d:%d:%d\n", vendor, device, bus, slot, func);
                    
                    /* Read BAR0 (Framebuffer Address) at Offset 0x10 */
                    bar0 = pci_read_config(bus, slot, func, 0x10);
                    
                    /* Mask out flag bits (usually lower 4 bits for memory BAR) */
                    bar0 &= 0xFFFFFFF0;
                    
                    found = 1;
                    goto device_found;
                }
            }
        }
    }

device_found:
    if (!found) {
        serial("[GPU] Bochs GPU not found.\n");
        return;
    }

    if (bar0 == 0) {
        serial("[GPU] Error: Bochs GPU BAR0 is 0.\n");
        return;
    }

    /* Check VBE ID to confirm BGA presence */
    uint16_t id = bochs_read(VBE_DISPI_INDEX_ID);
    if (id < 0xB0C0 || id > 0xB0C5) {
        serial("[GPU] Warning: Invalid Bochs VBE ID read (0x%x). Hardware might not be BGA compatible.\n", id);
        return;
    }
    serial("[GPU] Bochs VBE ID: 0x%x\n", id);

    /* Initialize Device Structure */
    bochs_device.type = GPU_TYPE_BOCHS;
    bochs_device.phys_addr = bar0;
    bochs_device.ops = &bochs_ops;
    bochs_device.virt_addr = (void*)BOCHS_LFB_VIRT_BASE;

    /* Set default mode parameters */
    uint32_t width = 1024;
    uint32_t height = 768;
    uint32_t bpp = 32;
    uint32_t fb_size = width * (bpp / 8) * height;

    /* Align size to page boundary */
    fb_size = (fb_size + 0xFFF) & 0xFFFFF000;

    /* Reserve PMM */
#ifndef __x86_64__
    pmm_reserve_area(bar0, fb_size);

    /* Map to Virtual Memory */
    uint32_t* active_pd = vmm_get_current_pd();
    vmm_map_region(active_pd, BOCHS_LFB_VIRT_BASE, bar0, fb_size, PAGE_PRESENT | PAGE_RW | PAGE_PCD | PAGE_PWT);
    
    /* Force TLB flush */
    asm volatile("mov %%cr3, %%eax; mov %%eax, %%cr3" ::: "eax", "memory");
#else
    /* On 64-bit, we map pages one by one */
    for (uint64_t i = 0; i < fb_size; i += 4096) {
        paging64_map_page(BOCHS_LFB_VIRT_BASE + i, (uint64_t)bar0 + i, 0x03 | 0x10 | 0x08); /* Present, RW, PCD, PWT */
    }
#endif

    /* Now that it's mapped, set the mode (which clears the screen safely) */
    if (bochs_set_mode(&bochs_device, width, height, bpp) != 0) {
        serial("[GPU] Error: Failed to set Bochs VBE mode.\n");
        return;
    }

    serial("[GPU] Bochs framebuffer mapped: phys=0x%x virt=0x%x size=%d\n", bar0, BOCHS_LFB_VIRT_BASE, fb_size);

    /* Register with Core */
    gpu_register_device(&bochs_device);
    serial("[GPU] Bochs GPU registered successfully.\n");
}
