// kernel/hardware/pci.cpp
#include "pci.h"
#ifndef __x86_64__
#include "../terminal.h"
#else
#include "../drivers/serial.h"
#define terminal_writestring serial_write_string
#define terminal_writehex serial_write_hex
#endif
#include "../arch/i386/io.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

/* ============================================================
   Forward declarations (ORDINE CORECTĂ PENTRU C++)
   ============================================================ */
static uint32_t pci_read(uint8_t bus, uint8_t device,
                         uint8_t function, uint8_t offset);

static uint16_t pci_get_vendor(uint8_t bus, uint8_t device, uint8_t function);
static uint16_t pci_get_device(uint8_t bus, uint8_t device, uint8_t function);

static uint8_t pci_get_class(uint8_t bus, uint8_t device, uint8_t function);
static uint8_t pci_get_subclass(uint8_t bus, uint8_t device, uint8_t function);
static uint8_t pci_get_progif(uint8_t bus, uint8_t device, uint8_t function);

static const char* pci_class_name(uint8_t cls, uint8_t sub, uint8_t prog);

/* ============================================================
   PCI config space access
   ============================================================ */
static uint32_t pci_read(uint8_t bus, uint8_t device,
                         uint8_t function, uint8_t offset)
{
    uint32_t address =
        (1U << 31) |
        ((uint32_t)bus << 16) |
        ((uint32_t)device << 11) |
        ((uint32_t)function << 8) |
        (offset & 0xFC);

    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static uint16_t pci_get_vendor(uint8_t bus, uint8_t device, uint8_t function)
{
    return pci_read(bus, device, function, 0x00) & 0xFFFF;
}

static uint16_t pci_get_device(uint8_t bus, uint8_t device, uint8_t function)
{
    return (pci_read(bus, device, function, 0x00) >> 16) & 0xFFFF;
}

static void pci_write(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value)
{
    uint32_t address =
        (1U << 31) |
        ((uint32_t)bus << 16) |
        ((uint32_t)device << 11) |
        ((uint32_t)function << 8) |
        (offset & 0xFC);

    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

/* ============================================================
   Class / Subclass helpers
   ============================================================ */
static uint8_t pci_get_class(uint8_t bus, uint8_t device, uint8_t function)
{
    return (pci_read(bus, device, function, 0x08) >> 24) & 0xFF;
}

static uint8_t pci_get_subclass(uint8_t bus, uint8_t device, uint8_t function)
{
    return (pci_read(bus, device, function, 0x08) >> 16) & 0xFF;
}

static uint8_t pci_get_progif(uint8_t bus, uint8_t device, uint8_t function)
{
    return (pci_read(bus, device, function, 0x08) >> 8) & 0xFF;
}

/* ============================================================
   Class decode (human-readable)
   ============================================================ */
static const char* pci_class_name(uint8_t cls, uint8_t sub, uint8_t prog)
{
    switch (cls) {

        case 0x01: /* Mass Storage */
            switch (sub) {
                case 0x01: return "IDE controller";
                case 0x06:
                    return (prog == 0x01)
                        ? "AHCI controller"
                        : "SATA controller";
                default:
                    return "Mass storage controller";
            }

        case 0x02: /* Network */
            return "Network controller";

        case 0x03: /* Display */
            switch (sub) {
                case 0x00: return "VGA compatible controller";
                default:   return "Display controller";
            }

        case 0x06: /* Bridge */
            switch (sub) {
                case 0x00: return "Host bridge";
                case 0x01: return "ISA bridge";
                default:   return "Bridge device";
            }

        case 0x0C: /* Serial bus */
            switch (sub) {
                case 0x03:
                    switch (prog) {
                        case 0x00: return "USB UHCI controller";
                        case 0x10: return "USB OHCI controller";
                        case 0x20: return "USB EHCI controller";
                        case 0x30: return "USB xHCI controller";
                        default:   return "USB controller";
                    }
                default:
                    return "Serial bus controller";
            }

        default:
            return "Unknown device";
    }
}

/* ============================================================
   Public entry point
   ============================================================ */
void pci_init(void)
{
    terminal_writestring("[pci] scanning PCI bus...\n\n");

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t function = 0; function < 8; function++) {

                uint16_t vendor = pci_get_vendor(bus, device, function);
                if (vendor == 0xFFFF)
                    continue;

                uint16_t dev = pci_get_device(bus, device, function);

                uint8_t cls  = pci_get_class(bus, device, function);
                uint8_t sub  = pci_get_subclass(bus, device, function);
                uint8_t prog = pci_get_progif(bus, device, function);

                terminal_writestring("[pci] ");
                terminal_writehex(vendor);
                terminal_writestring(":");
                terminal_writehex(dev);
                terminal_writestring(" (");
                terminal_writestring(pci_class_name(cls, sub, prog));
                terminal_writestring(")\n");
            }
        }
    }

    terminal_writestring("\n[pci] scan complete\n");
}

/* ============================================================
   Public API for Drivers (AHCI, etc.)
   ============================================================ */
extern "C" {

int pci_find_device_by_class(uint8_t class_code, uint8_t sub, uint8_t prog_if, uint8_t *out_bus, uint8_t *out_dev, uint8_t *out_func) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint16_t vendor = pci_get_vendor(bus, dev, func);
                if (vendor == 0xFFFF) continue;
                
                if (pci_get_class(bus, dev, func) == class_code &&
                    pci_get_subclass(bus, dev, func) == sub &&
                    pci_get_progif(bus, dev, func) == prog_if) {
                    *out_bus = (uint8_t)bus;
                    *out_dev = dev;
                    *out_func = func;
                    return 1;
                }
            }
        }
    }
    return 0;
}

uint32_t pci_read_bar32(uint8_t bus, uint8_t dev, uint8_t func, int bar_index) {
    return pci_read(bus, dev, func, 0x10 + bar_index * 4);
}

void pci_enable_busmaster(uint8_t bus, uint8_t dev, uint8_t func) {
    uint32_t cmd = pci_read(bus, dev, func, 0x04);
    cmd |= (1 << 2); /* Bit 2: Bus Master Enable */
    pci_write(bus, dev, func, 0x04, cmd);
}

}
