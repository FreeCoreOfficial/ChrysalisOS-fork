#include "usb_core.h"
#include "uhci.h"
#include "ehci.h"
#include "xhci.h"
#include "../drivers/serial.h"
#include "../arch/i386/io.h"
#include "../mem/kmalloc.h"
#include "../string.h"
#include "../input/keyboard_buffer.h"
#include "usb_hid.h"
#include "usb_hub.h"
#include "usb_msc.h"
#ifndef INSTALLER_BUILD
#include "../mm/vmm.h"
#include "../mm/paging.h"
#endif

/* PCI Configuration Port */
#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC

/* Helper to read PCI config 32-bit */
static uint32_t pci_read_config_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((1 << 31) | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC));
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

/* Helper to read PCI config 16-bit */
static uint16_t pci_read_config_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((1 << 31) | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC));
    outl(PCI_CONFIG_ADDRESS, address);
    return (uint16_t)((inl(PCI_CONFIG_DATA) >> ((offset & 2) * 8)) & 0xFFFF);
}

/* Helper to read PCI config 8-bit */
static uint8_t pci_read_config_byte(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((1 << 31) | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC));
    outl(PCI_CONFIG_ADDRESS, address);
    return (uint8_t)((inl(PCI_CONFIG_DATA) >> ((offset & 3) * 8)) & 0xFF);
}

/* Helper to write PCI config 32-bit */
static void pci_write_config_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = (uint32_t)((1 << 31) | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC));
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

/* Helper to write PCI config 16-bit */
static void pci_write_config_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value) {
    uint32_t address = (uint32_t)((1 << 31) | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC));
    outl(PCI_CONFIG_ADDRESS, address);
    uint32_t cur = inl(PCI_CONFIG_DATA);
    uint32_t shift = (offset & 2) * 8;
    cur &= ~(0xFFFFu << shift);
    cur |= ((uint32_t)value << shift);
    outl(PCI_CONFIG_DATA, cur);
}

static uint8_t pci_get_class(uint8_t bus, uint8_t slot, uint8_t func) {
    return pci_read_config_byte(bus, slot, func, 0x0B);
}

static uint8_t pci_get_subclass(uint8_t bus, uint8_t slot, uint8_t func) {
    return pci_read_config_byte(bus, slot, func, 0x0A);
}

static uint8_t pci_get_progif(uint8_t bus, uint8_t slot, uint8_t func) {
    return pci_read_config_byte(bus, slot, func, 0x09);
}

static void* usb_map_mmio(uint32_t phys, uint32_t size) {
#ifdef INSTALLER_BUILD
    (void)size;
    return (void *)(uintptr_t)phys;
#else
    static uint32_t usb_mmio_next = 0xE4000000u;
    uint32_t aligned = (size + 0xFFFu) & ~0xFFFu;
    uint32_t vaddr = usb_mmio_next;
    usb_mmio_next += aligned;
    vmm_map_region(vmm_get_current_pd(), vaddr, phys & ~0xFFFu, aligned,
                   PAGE_PRESENT | PAGE_RW | PAGE_PCD | PAGE_PWT);
    return (void *)(uintptr_t)(vaddr + (phys & 0xFFFu));
#endif
}

static void ehci_bios_handoff(uint8_t bus, uint8_t slot, uint8_t func,
                              volatile uint8_t* base) {
    if (!base)
        return;
    uint32_t hccparams = *(volatile uint32_t *)(base + 0x08);
    uint8_t eecp = (uint8_t)((hccparams >> 8) & 0xFF);
    if (!eecp)
        return;

    uint32_t legsup = pci_read_config_dword(bus, slot, func, eecp);
    if (legsup & (1u << 16)) {
        legsup |= (1u << 24);
        pci_write_config_dword(bus, slot, func, eecp, legsup);
        for (int i = 0; i < 1000000; ++i) {
            uint32_t cur = pci_read_config_dword(bus, slot, func, eecp);
            if ((cur & (1u << 16)) == 0)
                break;
        }
    }
    pci_write_config_dword(bus, slot, func, (uint8_t)(eecp + 4), 0);
}

static void ehci_route_ports_to_companion(volatile uint8_t* base) {
    if (!base)
        return;
    uint8_t caplen = *(volatile uint8_t *)base;
    uint32_t hcsparams = *(volatile uint32_t *)(base + 0x04);
    uint32_t n_ports = hcsparams & 0x0F;
    volatile uint8_t *op = base + caplen;
    volatile uint32_t *configflag = (volatile uint32_t *)(op + 0x40);
    *configflag = 0;
    for (uint32_t i = 0; i < n_ports; ++i) {
        volatile uint32_t *portsc = (volatile uint32_t *)(op + 0x44 + i * 4);
        *portsc |= (1u << 13);
    }
}

static void xhci_legacy_handoff(volatile uint8_t* base) {
    if (!base)
        return;
    uint32_t hccparams1 = *(volatile uint32_t *)(base + 0x10);
    uint16_t xecp = (uint16_t)((hccparams1 >> 16) & 0xFFFF);
    if (!xecp)
        return;
    uint32_t off = (uint32_t)xecp * 4u;
    while (off) {
        volatile uint32_t *cap = (volatile uint32_t *)(base + off);
        uint32_t val = *cap;
        uint8_t cap_id = (uint8_t)(val & 0xFF);
        uint8_t next = (uint8_t)((val >> 8) & 0xFF);
        if (cap_id == 1) {
            volatile uint32_t *legsup = (volatile uint32_t *)(base + off);
            volatile uint32_t *legctl = (volatile uint32_t *)(base + off + 4);
            if (*legsup & (1u << 16)) {
                *legsup |= (1u << 24);
                for (int i = 0; i < 1000000; ++i) {
                    if ((*legsup & (1u << 16)) == 0)
                        break;
                }
            }
            *legctl = 0;
            break;
        }
        if (!next)
            break;
        off += (uint32_t)next * 4u;
    }
}

/* Global USB State */
static uint8_t usb_addr_counter = 1;
static usb_hc_ops_t g_hc_ops = {0};

void usb_register_hc(const usb_hc_ops_t* ops) {
    if (!ops) return;
    g_hc_ops = *ops;
}

int usb_control_transfer(uint8_t addr, uint8_t endp, void* setup, void* data,
                         uint16_t len) {
    if (!g_hc_ops.control_transfer) return -1;
    return g_hc_ops.control_transfer(addr, endp, setup, data, len);
}

void* usb_interrupt_setup(uint8_t addr, uint8_t endp, void* data,
                          uint16_t len) {
    if (!g_hc_ops.interrupt_setup) return NULL;
    return g_hc_ops.interrupt_setup(addr, endp, data, len);
}

int usb_interrupt_poll(void* handle) {
    if (!g_hc_ops.interrupt_poll) return 0;
    return g_hc_ops.interrupt_poll(handle);
}

void usb_core_init(void) {
    serial_write_string("[USB] core initialized\r\n");

    /* Brute-force PCI scan for USB controllers (class-based) */
    bool uhci_initialized = false;
    volatile uint8_t *ehci_base = NULL;
    uint32_t ehci_mmio = 0;
    volatile uint8_t *xhci_base = NULL;
    uint32_t xhci_mmio = 0;
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint16_t vendor = pci_read_config_word(bus, slot, func, 0x00);
                if (vendor == 0xFFFF) continue;

                uint16_t device = pci_read_config_word(bus, slot, func, 0x02);
                uint8_t class_code = pci_get_class(bus, slot, func);
                uint8_t subclass = pci_get_subclass(bus, slot, func);
                uint8_t progif = pci_get_progif(bus, slot, func);

                /* USB controller class */
                if (class_code == 0x0C && subclass == 0x03) {
                    if (progif == 0x00) {
                        serial_printf("[USB] UHCI controller @ %02x:%02x.%d (vid=%x did=%x)\n",
                                      bus, slot, func, vendor, device);

                        /* Enable I/O space + bus mastering */
                        uint16_t cmd = pci_read_config_word(bus, slot, func, 0x04);
                        cmd |= 0x1; /* I/O space */
                        cmd |= 0x4; /* Bus Master */
                        pci_write_config_word(bus, slot, func, 0x04, cmd);

                        /* Find an I/O BAR (prefer BAR4, fallback to any IO BAR) */
                        uint32_t bar4 = pci_read_config_dword(bus, slot, func, 0x20);
                        uint32_t io_base = 0;
                        if (bar4 & 0x1) {
                            io_base = bar4 & ~0x3;
                        } else {
                            for (uint8_t bar = 0; bar < 6; ++bar) {
                                uint32_t barv = pci_read_config_dword(bus, slot, func, (uint8_t)(0x10 + bar * 4));
                                if (barv & 0x1) {
                                    io_base = barv & ~0x3;
                                    break;
                                }
                            }
                        }

                        if (io_base == 0) {
                            serial_write_string("[USB] UHCI has no I/O BAR, skipping\n");
                            continue;
                        }

                        /* Read IRQ Line (Offset 0x3C) */
                        uint8_t irq = pci_read_config_byte(bus, slot, func, 0x3C);

                        serial_printf("[USB] UHCI IO base = 0x%x\n", io_base);
                        serial_printf("[USB] UHCI IRQ = %d\n", irq);

                        if (!uhci_initialized) {
                            uhci_init(io_base, irq);
                            uhci_initialized = true;
                        }
                    } else if (progif == 0x10) {
                        serial_printf("[USB] OHCI controller detected @ %02x:%02x.%d (unsupported)\n",
                                      bus, slot, func);
                    } else if (progif == 0x20) {
                        serial_printf("[USB] EHCI controller detected @ %02x:%02x.%d (limited)\n",
                                      bus, slot, func);
                        if (!ehci_base) {
                            uint32_t bar0 = pci_read_config_dword(bus, slot, func, 0x10);
                            uint32_t mmio = bar0 & ~0xFU;
                            if (mmio) {
                                ehci_mmio = mmio;
                                ehci_base = (volatile uint8_t *)usb_map_mmio(mmio, 0x1000);
                                ehci_bios_handoff(bus, slot, func, ehci_base);
                            }
                        }
                    } else if (progif == 0x30) {
                        serial_printf("[USB] xHCI controller detected @ %02x:%02x.%d (limited)\n",
                                      bus, slot, func);
                        uint32_t bar0 = pci_read_config_dword(bus, slot, func, 0x10);
                        uint32_t mmio = bar0 & ~0xFU;
                        if (mmio) {
                            if (!xhci_base) {
                                xhci_mmio = mmio;
                                xhci_base = (volatile uint8_t *)usb_map_mmio(mmio, 0x4000);
                                xhci_legacy_handoff(xhci_base);
                            }
                        }
                    } else {
                        serial_printf("[USB] USB controller progIF=%x @ %02x:%02x.%d (unsupported)\n",
                                      progif, bus, slot, func);
                    }
                }

                /* Check for multifunction device to continue scanning functions */
                if (func == 0) {
                    uint8_t header_type = pci_read_config_byte(bus, slot, func, 0x0E);
                    if (!(header_type & 0x80)) {
                        break; // Not multifunction, skip other functions
                    }
                }
            }
        }
    }
    
    if (uhci_initialized && ehci_base) {
        ehci_route_ports_to_companion(ehci_base);
    }

    if (!uhci_initialized && ehci_mmio) {
        ehci_init(ehci_mmio);
        ehci_poll_ports();
    } else if (!uhci_initialized && !ehci_mmio && xhci_mmio) {
        xhci_init(xhci_mmio, xhci_base);
        xhci_poll_ports();
    }

    if (!uhci_initialized && !ehci_mmio && !xhci_mmio)
        serial_write_string("[USB] No UHCI controller found.\r\n");
}

/* --- Enumeration Logic --- */

void usb_attach_device(uint8_t port_id) {
    (void)port_id;
    serial_write_string("[USB] Starting enumeration...\r\n");

    /* 1. GET_DESCRIPTOR (DEVICE) - First 8 bytes to get MaxPacketSize0 */
    /* Actually prompt says 18 bytes. We'll ask for 18. */
    usb_setup_pkt_t setup;
    usb_dev_desc_t dev_desc;
    
    setup.bmRequestType = USB_RT_D2H | USB_RT_STANDARD | USB_RT_DEVICE;
    setup.bRequest = USB_REQ_GET_DESCRIPTOR;
    setup.wValue = (USB_DESC_DEVICE << 8) | 0;
    setup.wIndex = 0;
    setup.wLength = 18;

    serial_write_string("[USB] GET_DESCRIPTOR (DEVICE)\r\n");
    if (usb_control_transfer(0, 0, &setup, &dev_desc, 18) < 0) {
        serial_write_string("[USB] Failed to get device descriptor\r\n");
        return;
    }
    serial_write_string("[USB] device descriptor received\r\n");
    serial_printf("[USB] MaxPacketSize0 = %d\n", dev_desc.bMaxPacketSize0);
    serial_printf("[USB] Class: %x, SubClass: %x, Protocol: %x\n", 
                  dev_desc.bDeviceClass, dev_desc.bDeviceSubClass, dev_desc.bDeviceProtocol);
    serial_printf("[USB] VID: %x, PID: %x\n", dev_desc.idVendor, dev_desc.idProduct);


    /* 2. SET_ADDRESS */
    uint8_t new_addr = usb_addr_counter++;
    setup.bmRequestType = USB_RT_H2D | USB_RT_STANDARD | USB_RT_DEVICE;
    setup.bRequest = USB_REQ_SET_ADDRESS;
    setup.wValue = new_addr;
    setup.wIndex = 0;
    setup.wLength = 0;

    serial_printf("[USB] SET_ADDRESS = %d\n", new_addr);
    if (usb_control_transfer(0, 0, &setup, 0, 0) < 0) {
        serial_write_string("[USB] Failed to set address\r\n");
        return;
    }
    serial_write_string("[USB] device address set\r\n");

    /* 3. GET_DESCRIPTOR (CONFIGURATION) - Full */
    /* Read 9 bytes first to get total length */
    uint8_t buf[256]; /* Buffer for config descriptor */
    
    setup.bmRequestType = USB_RT_D2H | USB_RT_STANDARD | USB_RT_DEVICE;
    setup.bRequest = USB_REQ_GET_DESCRIPTOR;
    setup.wValue = (USB_DESC_CONFIGURATION << 8) | 0;
    setup.wIndex = 0;
    setup.wLength = 9;

    serial_write_string("[USB] GET_DESCRIPTOR (CONFIG)\r\n");
    if (usb_control_transfer(new_addr, 0, &setup, buf, 9) < 0) return;
    
    uint16_t total_len = *(uint16_t*)(buf + 2);
    serial_printf("[USB] configuration descriptor length = %d\n", total_len);

    if (total_len > 255 || total_len < 9) {
        serial_write_string("[USB] Invalid configuration descriptor length. Aborting.\r\n");
        return;
    }
    
    /* Read full config */
    setup.wLength = total_len;
    if (usb_control_transfer(new_addr, 0, &setup, buf, total_len) < 0) return;

    /* Parse Config */
    /* We look for Interface Descriptor (Class 3 = HID) */
    uint8_t* ptr = buf;
    uint8_t* end = buf + total_len;
    
    /* Default to device class if interface class is 0 */
    uint8_t device_class = dev_desc.bDeviceClass;

    while (ptr < end) {
        uint8_t len = ptr[0];
        uint8_t type = ptr[1];
        if (len == 0) break; // Avoid infinite loop on malformed descriptor
        
        if (type == USB_DESC_INTERFACE) {
            serial_write_string("[USB] interface found\r\n");
            device_class = ptr[5]; // Use interface class
            break; // For now, we only care about the first interface
        }
        ptr += len;
    }

    /* Reset pointer to parse again for the specific driver */
    ptr = buf;

    /* Dispatch to class driver */
    switch (device_class) {
        case 0x03: /* HID */
            usb_hid_init(new_addr, ptr, total_len);
            break;
        case 0x08: /* Mass Storage */
            usb_msc_init(new_addr, ptr, total_len);
            break;
        case 0x09: /* Hub */
            usb_hub_init(new_addr, ptr, total_len);
            break;
        default:
            serial_printf("[USB] Unknown or unsupported device class: %x\n", device_class);
            break;
    }

    /* 4. SET_CONFIGURATION (Common for most devices) */
    setup.bmRequestType = USB_RT_H2D | USB_RT_STANDARD | USB_RT_DEVICE;
    setup.bRequest = USB_REQ_SET_CONFIGURATION;
    setup.wValue = 1; /* Config 1 */
    setup.wIndex = 0;
    setup.wLength = 0;

    serial_printf("[USB] SET_CONFIGURATION = 1\n");
    if (usb_control_transfer(new_addr, 0, &setup, 0, 0) < 0) {
        serial_write_string("[USB] Failed to set configuration\r\n");
        return;
    }
    serial_write_string("[USB] device configured\r\n");
}

void usb_poll(void) {
    // This will now be handled by class drivers
    usb_hid_poll();
    ehci_poll_ports();
    xhci_poll_ports();
}
