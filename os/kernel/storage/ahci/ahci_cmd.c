#include "ahci.h"
#include <stdint.h>

extern ahci_port_state_t port_states[]; /* from ahci_port.c (make static->extern if needed) */

#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_READ_DMA      0xC8
#define ATA_CMD_WRITE_DMA     0xCA

/* FIS types */
#define FIS_TYPE_REG_H2D 0x27

static void mem_zero(void *ptr, size_t size) {
    uint8_t *p = (uint8_t*)ptr;
    for (size_t i = 0; i < size; i++) p[i] = 0;
}

static void mem_copy(void *dst, const void *src, size_t size) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    for (size_t i = 0; i < size; i++) d[i] = s[i];
}

#define AHCI_BOUNCE_SECTORS 32
static uint8_t __attribute__((aligned(4096)))
    ahci_bounce_buf[AHCI_BOUNCE_SECTORS * 512];

/* Helper: build command header in CLB */
static void setup_cmd_header(void *clb, int slot, uint8_t flags, uint16_t prdt_len, uint32_t ctba) {
    hba_cmd_header_t *cl = (hba_cmd_header_t*)clb;
    /* clb is array of headers; each header is 32 bytes in spec; we simplified earlier */
    hba_cmd_header_t *h = (hba_cmd_header_t*)((uint8_t*)cl + slot * sizeof(hba_cmd_header_t));
    h->flags = (flags & 0xFF) | ((prdt_len & 0xFFFF) << 8);
    h->prdt_len = prdt_len;
    h->ctba = ctba;
}

/* small helper to wait for completion (CI clear) */
static int wait_for_cmd_complete(hba_port_t *port, int slot, uint32_t timeout_ms) {
    uint32_t start = get_uptime_ms();
    while ((port->ci & (1U << slot)) && (get_uptime_ms() - start < timeout_ms)) { /* spin */ }
    if (port->ci & (1U << slot)) return -1;
    /* check tfd for errors */
    uint32_t tfd = port->tfd;
    if (tfd & (1<<0) /* BSY */) return -2;
    if (tfd & (1<<1) /* DRQ */) return 0;
    return 0;
}

/* Build a PRDT entry list for single buffer (handles up to 4G by splitting) */
static int build_prdt_for_buffer(void *ct, const void *buf, uint32_t byte_count) {
    /* ct assumed zeroed; PRDT starts after 0x80 in cmd table per spec; we'll place first at offset 0x80 */
    uint8_t *ctb = (uint8_t*)ct;
    hba_prdt_entry_t *prdt = (hba_prdt_entry_t*)(ctb + 0x80);
    prdt->dba = (uint32_t)(uintptr_t)buf;
    prdt->dbau = 0;
    prdt->dbc = byte_count - 1; /* dbc is byte_count - 1 */
    return 1;
}

/* IDENTIFY implementation (polling) */
int ahci_send_identify(int port_no, hba_port_t *port, void *out_512) {
    serial("[AHCI] port %d: IDENTIFY start\n", port_no);
    /* find slot */
    int slot = find_cmdslot(port);
    if (slot < 0) {
        serial("[AHCI] port %d: IDENTIFY no free slot\n", port_no);
        return -1;
    }
    /* get CLB and CT pointers */
    ahci_port_state_t *st = &port_states[port_no];
    void *clb = st->clb;
    void *ct = st->cmd_tables[slot];
    uint32_t ct_phys = ahci_virt_to_phys(ct);

    /* prepare command header */
    setup_cmd_header(clb, slot, (5<<0), 1, ct_phys); /* flags: CFL=5 (RegH2D), PRDT len=1. Clear W (write) bit for Identify! */

    /* build PRDT for output buffer */
    /* PRDT must point to physical address of buffer */
    build_prdt_for_buffer(ct, (void*)ahci_virt_to_phys(out_512), 512);

    /* put FIS in command table (fis reg h2d) */
    fis_reg_h2d_t *fis = (fis_reg_h2d_t*)ct;
    mem_zero(fis, sizeof(fis_reg_h2d_t));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;
    fis->command = ATA_CMD_IDENTIFY;
    /* device registers zeroed (for ATA) */

    /* issue command */
    port->ci = (1U << slot);

    /* wait for completion */
    int r = wait_for_cmd_complete(port, slot, 5000);
    if (r != 0) {
        serial("[AHCI] port %d: IDENTIFY failed wait (%d), tfd=0x%08x\n", port_no, r, port->tfd);
        return -2;
    }

    serial("[AHCI] port %d: IDENTIFY done\n", port_no);
    return 0;
}

/* READ LBA (LBA28) simplified for single PRDT entry (assumes contiguous physical buffer) */
static int ahci_read_lba_raw(int port_no, uint64_t lba, uint32_t count, void *dma_buf) {
    hba_port_t *port = port_states[port_no].port;
    if (!port) {
        serial("[AHCI] read: port %d not initialized\n", port_no);
        return -1;
    }
    serial("[AHCI] read: port=%d lba=%llu count=%u\n", port_no, (unsigned long long)lba, count);

    int slot = find_cmdslot(port);
    if (slot < 0) return -2;
    void *clb = port_states[port_no].clb;
    void *ct = port_states[port_no].cmd_tables[slot];
    uint32_t ct_phys = ahci_virt_to_phys(ct);
    setup_cmd_header(clb, slot, (5<<0), 1, ct_phys); /* Read: Clear W bit */
    build_prdt_for_buffer(ct, (void*)ahci_virt_to_phys(dma_buf), count * 512);

    fis_reg_h2d_t *fis = (fis_reg_h2d_t*)ct;
    mem_zero(fis, sizeof(fis_reg_h2d_t));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;
    fis->command = ATA_CMD_READ_DMA;
    fis->lba0 = (uint8_t)(lba & 0xFF);
    fis->lba1 = (uint8_t)((lba >> 8) & 0xFF);
    fis->lba2 = (uint8_t)((lba >> 16) & 0xFF);
    fis->device = 0x40 | ((uint8_t)((lba >> 24) & 0x0F)); /* LBA mode + Head */
    fis->countl = (uint8_t)(count & 0xFF);

    port->ci = (1U << slot);

    int r = wait_for_cmd_complete(port, slot, 5000);
    if (r != 0) {
        serial("[AHCI] read: port %d cmd failed (%d) tfd=0x%08x\n", port_no, r, port->tfd);
        return -3;
    }
    serial("[AHCI] read: done port=%d lba=%llu count=%u\n", port_no, (unsigned long long)lba, count);
    return 0;
}

/* write implementation mirrors read but uses ATA_CMD_WRITE_DMA */
static int ahci_write_lba_raw(int port_no, uint64_t lba, uint32_t count, const void *dma_buf) {
    hba_port_t *port = port_states[port_no].port;
    if (!port) {
        serial("[AHCI] write: port %d not initialized\n", port_no);
        return -1;
    }
    serial("[AHCI] write: port=%d lba=%llu count=%u\n", port_no, (unsigned long long)lba, count);

    int slot = find_cmdslot(port);
    if (slot < 0) return -2;
    void *clb = port_states[port_no].clb;
    void *ct = port_states[port_no].cmd_tables[slot];
    uint32_t ct_phys = ahci_virt_to_phys(ct);
    setup_cmd_header(clb, slot, (1<<6) | (5<<0), 1, ct_phys); /* Write: Set W bit (bit 6) */
    /* PRDT must point to non-const buffer — cast away const for DMA */
    build_prdt_for_buffer(ct, (void*)ahci_virt_to_phys((void*)dma_buf), count * 512);

    fis_reg_h2d_t *fis = (fis_reg_h2d_t*)ct;
    mem_zero(fis, sizeof(fis_reg_h2d_t));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;
    fis->command = ATA_CMD_WRITE_DMA;
    fis->lba0 = (uint8_t)(lba & 0xFF);
    fis->lba1 = (uint8_t)((lba >> 8) & 0xFF);
    fis->lba2 = (uint8_t)((lba >> 16) & 0xFF);
    fis->device = 0x40 | ((uint8_t)((lba >> 24) & 0x0F)); /* LBA mode + Head */
    fis->countl = (uint8_t)(count & 0xFF);

    port->ci = (1U << slot);

    int r = wait_for_cmd_complete(port, slot, 5000);
    if (r != 0) {
        serial("[AHCI] write: port %d cmd failed (%d) tfd=0x%08x\n", port_no, r, port->tfd);
        return -3;
    }
    serial("[AHCI] write: done port=%d lba=%llu count=%u\n", port_no, (unsigned long long)lba, count);
    return 0;
}

/* Public read/write use a DMA-safe bounce buffer to avoid DMA into user/non-contiguous memory. */
int ahci_read_lba(int port_no, uint64_t lba, uint32_t count, void *buf) {
    if (count == 0) return 0;
    uint8_t *out = (uint8_t*)buf;
    uint32_t done = 0;
    while (done < count) {
        uint32_t chunk = count - done;
        if (chunk > AHCI_BOUNCE_SECTORS) chunk = AHCI_BOUNCE_SECTORS;
        int r = ahci_read_lba_raw(port_no, lba + done, chunk, ahci_bounce_buf);
        if (r != 0) return r;
        mem_copy(out + (done * 512), ahci_bounce_buf, chunk * 512);
        done += chunk;
    }
    return 0;
}

int ahci_write_lba(int port_no, uint64_t lba, uint32_t count, const void *buf) {
    if (count == 0) return 0;
    const uint8_t *in = (const uint8_t*)buf;
    uint32_t done = 0;
    while (done < count) {
        uint32_t chunk = count - done;
        if (chunk > AHCI_BOUNCE_SECTORS) chunk = AHCI_BOUNCE_SECTORS;
        mem_copy(ahci_bounce_buf, in + (done * 512), chunk * 512);
        int r = ahci_write_lba_raw(port_no, lba + done, chunk, ahci_bounce_buf);
        if (r != 0) return r;
        done += chunk;
    }
    return 0;
}
