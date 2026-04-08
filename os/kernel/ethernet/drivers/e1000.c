#include "e1000.h"
#include "../net_device.h"
#include "../eth.h"
#include "../../mm/kmalloc.h"
#include "../../string.h"
#include "../../mm/vmm.h"
#include "../../arch/i386/io.h"
#include "../../interrupts/irq.h"

/* Minimal PCI Config Access */
#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

static uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t addr = (1 << 31) | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

extern void serial(const char *fmt, ...);

#define E1000_NUM_RX_DESC 32
#define E1000_NUM_TX_DESC 8

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed)) e1000_rx_desc;

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __attribute__((packed)) e1000_tx_desc;

#define E1000_CMD_EOP  (1 << 0)
#define E1000_CMD_IFCS (1 << 1)
#define E1000_CMD_RS   (1 << 3)

static volatile uint8_t* mmio_base = 0;
static e1000_rx_desc* rx_descs;
static e1000_tx_desc* tx_descs;
static uint8_t* rx_buffers[E1000_NUM_RX_DESC];
static uint16_t rx_cur = 0;
static uint16_t tx_cur = 0;
static net_device_t e1000_dev;

static void e1000_write(uint16_t reg, uint32_t val) {
    *(volatile uint32_t*)(mmio_base + reg) = val;
}

static uint32_t e1000_read(uint16_t reg) {
    return *(volatile uint32_t*)(mmio_base + reg);
}

static int e1000_eeprom_read(uint8_t addr) {
    uint32_t tmp = 0;
    e1000_write(E1000_EEPROM, 1 | ((uint32_t)addr << 8));
    while (!((tmp = e1000_read(E1000_EEPROM)) & (1 << 4)));
    return (tmp >> 16) & 0xFFFF;
}

static int e1000_send(net_device_t* dev, const void* data, size_t len) {
    (void)dev;
    tx_descs[tx_cur].addr = (uint64_t)vmm_virt_to_phys((void*)data);
    tx_descs[tx_cur].length = len;
    /* Enable End of Packet, Insert FCS, Report Status */
    tx_descs[tx_cur].cmd = E1000_CMD_EOP | E1000_CMD_IFCS | E1000_CMD_RS;
    tx_descs[tx_cur].status = 0;
    
    uint16_t old_cur = tx_cur;
    tx_cur = (tx_cur + 1) % E1000_NUM_TX_DESC;
    e1000_write(E1000_TDT, tx_cur);

    /* Wait for packet to be sent (with timeout) */
    int timeout = 10000000; /* Increased timeout for stability */
    while (!(tx_descs[old_cur].status & 0xFF)) {
        if (--timeout == 0) {
            serial("[E1000] TX Timeout! Packet might be dropped.\n");
            return -1;
        }
        asm volatile("pause");
    }
    
    return 0;
}

static int e1000_poll(net_device_t* dev) {
    int received = 0;
    while ((rx_descs[rx_cur].status & 1)) {
        uint8_t* buf = rx_buffers[rx_cur];
        uint16_t len = rx_descs[rx_cur].length;

        eth_handle_packet(dev, buf, len);

        rx_descs[rx_cur].status = 0;
        e1000_write(E1000_RDT, rx_cur);
        rx_cur = (rx_cur + 1) % E1000_NUM_RX_DESC;
        received++;
    }
    return received;
}

static void e1000_irq_handler(registers_t* r) {
    (void)r;
    uint32_t status = e1000_read(E1000_ICR);

    /* Check for Link Status Change */
    if (status & 0x04) {
        serial("[E1000] Link Status Changed: %s\n", (e1000_read(E1000_STATUS) & 2) ? "UP" : "DOWN");
    }

    /* Check for Receive Interrupts (RXDMT0 or RXT0) */
    if (status & 0x80 || status & 0x40) {
        e1000_poll(&e1000_dev);
    }
}

int e1000_init(void) {
    /* PCI Scan for 8086:100E (QEMU e1000) */
    uint32_t pci_id = 0;
    uint8_t bus, slot;
    int found = 0;

    for (bus = 0; bus < 255; bus++) {
        for (slot = 0; slot < 32; slot++) {
            uint32_t id = pci_read(bus, slot, 0, 0);
            if ((id & 0xFFFF) == 0x8086) {
                uint16_t dev = (id >> 16);
                if (dev == 0x100E || dev == 0x1004 || dev == 0x100F) {
                    pci_id = id;
                    found = 1;
                    goto pci_found;
                }
            }
        }
    }
pci_found:
    if (!found) return -1;

    serial("[E1000] Found device %04x:%04x at %d:%d\n", pci_id&0xFFFF, pci_id>>16, bus, slot);

    /* Read BAR0 (MMIO) */
    uint32_t bar0 = pci_read(bus, slot, 0, 0x10);
    if (!(bar0 & 0x1)) { // Memory Space BAR
        uint32_t mmio_phys = bar0 & 0xFFFFFFF0;
        
        /* Map MMIO to a safe virtual address */
        /* For now we use identity mapping but ensure it's mapped with PCD/PWT for hardware */
        uint32_t* pd = vmm_get_current_pd();
        vmm_map_region(pd, mmio_phys, mmio_phys, 128 * 1024, PAGE_PRESENT | PAGE_RW | PAGE_PCD | PAGE_PWT);
        mmio_base = (uint8_t*)mmio_phys;
        serial("[E1000] MMIO mapped at %x (Phys: %x)\n", mmio_base, mmio_phys);
    } else {
        serial("[E1000] Error: BAR0 is not MMIO. Networking will fail.\n");
        return -1;
    }

    /* Read IRQ */
    uint32_t irq_info = pci_read(bus, slot, 0, 0x3C);
    uint8_t irq = irq_info & 0xFF;

    /* Enable Bus Mastering */
    uint32_t pci_cmd = pci_read(bus, slot, 0, 0x04);
    outl(PCI_CONFIG_ADDR, (1 << 31) | (bus << 16) | (slot << 11) | 0x04);
    outl(PCI_CONFIG_DATA, pci_cmd | 0x4); // Bus Master

    /* Set Link Up */
    uint32_t ctrl = e1000_read(E1000_CTRL);
    e1000_write(E1000_CTRL, ctrl | (1 << 6)); /* SLU */

    /* Clear MTA */
    for (int i = 0; i < 128; i++) {
        e1000_write(E1000_MTA + i * 4, 0);
    }

    /* Read MAC */
    uint16_t mac16[3];
    mac16[0] = e1000_eeprom_read(0);
    mac16[1] = e1000_eeprom_read(1);
    mac16[2] = e1000_eeprom_read(2);
    
    e1000_dev.mac[0] = mac16[0] & 0xFF;
    e1000_dev.mac[1] = mac16[0] >> 8;
    e1000_dev.mac[2] = mac16[1] & 0xFF;
    e1000_dev.mac[3] = mac16[1] >> 8;
    e1000_dev.mac[4] = mac16[2] & 0xFF;
    e1000_dev.mac[5] = mac16[2] >> 8;

    /* Setup Device Struct */
    strcpy(e1000_dev.name, "e1000");
    e1000_dev.send = e1000_send;
    e1000_dev.poll = e1000_poll;
    e1000_dev.ip      = 0; /* 0.0.0.0 (Wait for DHCP) */
    e1000_dev.gateway = 0;
    e1000_dev.subnet  = 0;
    
    net_register_device(&e1000_dev);

    /* Init RX */
    rx_descs = (e1000_rx_desc*)kmalloc_aligned(sizeof(e1000_rx_desc) * E1000_NUM_RX_DESC, 16);
    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        rx_buffers[i] = (uint8_t*)kmalloc(2048);
        rx_descs[i].addr = (uint64_t)vmm_virt_to_phys(rx_buffers[i]);
        rx_descs[i].status = 0;
    }

    e1000_write(E1000_RDBAL, (uint32_t)vmm_virt_to_phys(rx_descs));
    e1000_write(E1000_RDBAH, 0);
    e1000_write(E1000_RDLEN, sizeof(e1000_rx_desc) * E1000_NUM_RX_DESC);
    e1000_write(E1000_RDH, 0);
    e1000_write(E1000_RDT, E1000_NUM_RX_DESC - 1);
    e1000_write(E1000_RCTL, RCTL_EN | RCTL_SBP | RCTL_UPE | RCTL_MPE | RCTL_LPE | RCTL_BAM);

    /* Init TX */
    tx_descs = (e1000_tx_desc*)kmalloc_aligned(sizeof(e1000_tx_desc) * E1000_NUM_TX_DESC, 16);
    memset(tx_descs, 0, sizeof(e1000_tx_desc) * E1000_NUM_TX_DESC);

    e1000_write(E1000_TDBAL, (uint32_t)vmm_virt_to_phys(tx_descs));
    e1000_write(E1000_TDBAH, 0);
    e1000_write(E1000_TDLEN, sizeof(e1000_tx_desc) * E1000_NUM_TX_DESC);
    e1000_write(E1000_TDH, 0);
    e1000_write(E1000_TDT, 0);
    e1000_write(E1000_TCTL, TCTL_EN | TCTL_PSP | (0x40 << 12)); /* COLD=40h */
    e1000_write(E1000_TIPG, 0x0060200A); /* 10, 8, 6 */

    /* Enable Interrupts */
    irq_install_handler(irq, e1000_irq_handler);
    /* IMS: TXDW, LSC, RXDMT0, RXT0 */
    e1000_write(E1000_IMS, 0x01 | 0x04 | 0x40 | 0x80);
    e1000_read(E1000_ICR);

    return 0;
}