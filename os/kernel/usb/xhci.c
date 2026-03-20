#include "xhci.h"
#include "usb_core.h"
#include "../drivers/serial.h"
#include "../mem/kmalloc.h"
#include "../string.h"
#include "../hardware/hpet.h"
#ifndef INSTALLER_BUILD
#include "../mm/vmm.h"
#include "../mm/paging.h"
#endif

#define XHCI_CAP_HCSPARAMS1 0x04
#define XHCI_CAP_HCCPARAMS1 0x10
#define XHCI_CAP_DBOFF      0x14
#define XHCI_CAP_RTSOFF     0x18

#define XHCI_OP_USBCMD      0x00
#define XHCI_OP_USBSTS      0x04
#define XHCI_OP_PAGESIZE    0x08
#define XHCI_OP_DNCTRL      0x14
#define XHCI_OP_CRCR        0x18
#define XHCI_OP_DCBAAP      0x30
#define XHCI_OP_CONFIG      0x38
#define XHCI_OP_PORTS       0x400

#define XHCI_RT_IMAN        0x00
#define XHCI_RT_IMOD        0x04
#define XHCI_RT_ERSTSZ      0x08
#define XHCI_RT_ERSTBA      0x10
#define XHCI_RT_ERDP        0x18

#define XHCI_USBCMD_RUN     (1u << 0)
#define XHCI_USBCMD_RESET   (1u << 1)
#define XHCI_USBCMD_INTE    (1u << 2)

#define XHCI_USBSTS_HCH     (1u << 0)
#define XHCI_USBSTS_CNR     (1u << 11)

#define XHCI_PORTSC_CCS     (1u << 0)
#define XHCI_PORTSC_PED     (1u << 1)
#define XHCI_PORTSC_PR      (1u << 4)
#define XHCI_PORTSC_PP      (1u << 9)
#define XHCI_PORTSC_CSC     (1u << 17)
#define XHCI_PORTSC_PEC     (1u << 18)
#define XHCI_PORTSC_WRC     (1u << 19)
#define XHCI_PORTSC_PRC     (1u << 21)
#define XHCI_PORTSC_WPR     (1u << 31)

#define XHCI_PORT_SPEED_MASK  (0xF << 10)

typedef struct __attribute__((packed)) {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
} xhci_trb_t;

typedef struct {
    xhci_trb_t *ring;
    uint32_t size;
    uint32_t enqueue;
    uint8_t cycle;
} xhci_ring_t;

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint32_t size;
    uint32_t rsvd;
} xhci_erst_entry_t;

typedef struct {
    uint8_t slot;
    uint8_t port;
    uint8_t speed;
    uint8_t addr;
    uint16_t max_packet;
    int ready;
    int intr_configured;
    uint8_t intr_dci;
    xhci_ring_t ctrl_ring;
    void *dev_ctx;
    void *input_ctx;
} xhci_device_t;

typedef struct {
    uint8_t dci;
    xhci_ring_t ring;
    uint64_t pending_trb;
    uint8_t *buffer;
    uint16_t len;
} xhci_intr_handle_t;

enum {
    TRB_TYPE_NORMAL = 1,
    TRB_TYPE_SETUP = 2,
    TRB_TYPE_DATA = 3,
    TRB_TYPE_STATUS = 4,
    TRB_TYPE_LINK = 6,

    TRB_CMD_ENABLE_SLOT = 9,
    TRB_CMD_ADDRESS_DEVICE = 11,
    TRB_CMD_CONFIGURE_ENDPOINT = 12,
    TRB_CMD_EVAL_CONTEXT = 13,

    TRB_EVT_TRANSFER = 32,
    TRB_EVT_CMD_COMPLETE = 33
};

#define TRB_TYPE_SHIFT 10
#define TRB_CYCLE      (1u << 0)
#define TRB_LINK_TC    (1u << 1)
#define TRB_IOC        (1u << 5)
#define TRB_IDT        (1u << 6)
#define TRB_DIR_IN     (1u << 16)

#define SETUP_TT_NO_DATA  (0u << 16)
#define SETUP_TT_OUT_DATA (2u << 16)
#define SETUP_TT_IN_DATA  (3u << 16)

static volatile uint8_t *xhci_base = NULL;
static volatile uint8_t *xhci_op = NULL;
static volatile uint8_t *xhci_rt = NULL;
static volatile uint8_t *xhci_db = NULL;
static uint32_t xhci_caplen = 0;
static uint32_t xhci_max_ports = 0;
static uint32_t xhci_max_slots = 0;
static int xhci_ctx_size = 32;
static int xhci_ready = 0;

static xhci_ring_t xhci_cmd_ring;
static xhci_trb_t *xhci_evt_ring = NULL;
static xhci_erst_entry_t *xhci_erst = NULL;
static uint32_t xhci_evt_size = 256;
static uint32_t xhci_evt_deq = 0;
static uint8_t xhci_evt_cycle = 1;

static uint64_t *xhci_dcbaa = NULL;
static xhci_device_t xhci_dev = {0};

static inline uint32_t xhci_read32(volatile uint8_t *base, uint32_t off) {
    return *(volatile uint32_t *)(base + off);
}

static inline void xhci_write32(volatile uint8_t *base, uint32_t off, uint32_t val) {
    *(volatile uint32_t *)(base + off) = val;
}

static inline void xhci_write64(volatile uint8_t *base, uint32_t off, uint64_t val) {
    *(volatile uint32_t *)(base + off) = (uint32_t)val;
    *(volatile uint32_t *)(base + off + 4) = (uint32_t)(val >> 32);
}

static uint64_t xhci_phys(void *ptr) {
#ifdef INSTALLER_BUILD
    return (uint64_t)(uintptr_t)ptr;
#else
    return (uint64_t)vmm_virt_to_phys(ptr);
#endif
}

static void xhci_mmio_sync(void) {
    asm volatile("" ::: "memory");
}

static void xhci_ring_init(xhci_ring_t *ring, uint32_t size) {
    ring->ring = (xhci_trb_t *)kmalloc_aligned(size * sizeof(xhci_trb_t), 64);
    memset(ring->ring, 0, size * sizeof(xhci_trb_t));
    ring->size = size;
    ring->enqueue = 0;
    ring->cycle = 1;

    xhci_trb_t *link = &ring->ring[size - 1];
    link->parameter = xhci_phys(ring->ring);
    link->status = 0;
    link->control = (TRB_TYPE_LINK << TRB_TYPE_SHIFT) | TRB_LINK_TC | ring->cycle;
}

static uint64_t xhci_ring_enqueue(xhci_ring_t *ring, xhci_trb_t trb) {
    uint32_t write_idx = ring->enqueue;
    trb.control |= ring->cycle;
    ring->ring[write_idx] = trb;
    xhci_mmio_sync();

    uint32_t next = write_idx + 1;
    if (next >= ring->size - 1) {
        next = 0;
        ring->cycle ^= 1;
    }
    ring->enqueue = next;
    return xhci_phys(&ring->ring[write_idx]);
}

static int xhci_event_dequeue(xhci_trb_t *out) {
    xhci_trb_t *evt = &xhci_evt_ring[xhci_evt_deq];
    if ((evt->control & 1u) != xhci_evt_cycle)
        return 0;
    *out = *evt;
    xhci_evt_deq++;
    if (xhci_evt_deq >= xhci_evt_size) {
        xhci_evt_deq = 0;
        xhci_evt_cycle ^= 1;
    }
    uint64_t erdp = xhci_phys(&xhci_evt_ring[xhci_evt_deq]) | (1ull << 3);
    xhci_write64((volatile uint8_t *)xhci_rt, XHCI_RT_ERDP, erdp);
    return 1;
}

static int xhci_wait_event(uint64_t trb_ptr, uint8_t expect_type,
                           xhci_trb_t *out) {
    int timeout = 2000;
    while (timeout-- > 0) {
        xhci_trb_t evt;
        if (xhci_event_dequeue(&evt)) {
            uint8_t type = (evt.control >> TRB_TYPE_SHIFT) & 0x3F;
            if (expect_type && type != expect_type)
                continue;
            if (trb_ptr && evt.parameter != trb_ptr)
                continue;
            if (out)
                *out = evt;
            return (evt.status >> 24) & 0xFF;
        }
        hpet_delay_ms(1);
    }
    return -1;
}

static int xhci_cmd(xhci_trb_t cmd, xhci_trb_t *out) {
    uint64_t cmd_ptr = xhci_ring_enqueue(&xhci_cmd_ring, cmd);
    xhci_write32((volatile uint8_t *)xhci_db, 0, 0);
    int cc = xhci_wait_event(cmd_ptr, TRB_EVT_CMD_COMPLETE, out);
    if (cc < 0)
        return -1;
    return cc == 1 ? 0 : -1;
}

static uint16_t xhci_initial_mps(uint8_t speed) {
    switch (speed) {
        case 1: return 8;
        case 2: return 8;
        case 3: return 64;
        default: return 512;
    }
}

static void xhci_build_slot_ctx(uint8_t *slot_ctx, uint8_t port, uint8_t speed,
                                uint8_t addr, uint8_t ctx_entries) {
    uint32_t *d = (uint32_t *)slot_ctx;
    memset(slot_ctx, 0, xhci_ctx_size);
    d[0] = ((uint32_t)speed << 20) | ((uint32_t)ctx_entries << 27);
    d[1] = ((uint32_t)(port + 1) << 16);
    d[3] = (uint32_t)addr;
}

static void xhci_build_ep0_ctx(uint8_t *ep_ctx, uint16_t max_packet,
                               uint64_t tr_deq) {
    uint32_t *d = (uint32_t *)ep_ctx;
    memset(ep_ctx, 0, xhci_ctx_size);
    d[1] = (3u << 1) | (4u << 3) | ((uint32_t)max_packet << 16);
    d[2] = (uint32_t)tr_deq;
    d[3] = (uint32_t)(tr_deq >> 32);
    d[4] = 8;
}

static void xhci_build_int_ctx(uint8_t *ep_ctx, uint16_t max_packet,
                               uint8_t interval, uint64_t tr_deq) {
    uint32_t *d = (uint32_t *)ep_ctx;
    memset(ep_ctx, 0, xhci_ctx_size);
    d[0] = ((uint32_t)interval << 16);
    d[1] = (3u << 1) | (7u << 3) | ((uint32_t)max_packet << 16);
    d[2] = (uint32_t)tr_deq;
    d[3] = (uint32_t)(tr_deq >> 32);
    d[4] = max_packet;
}

static int xhci_update_ep0(uint16_t max_packet) {
    uint8_t *icc = (uint8_t *)xhci_dev.input_ctx;
    memset(icc, 0, xhci_ctx_size);
    *(uint32_t *)icc = 0;
    *(uint32_t *)(icc + 4) = (1u << 0) | (1u << 1);

    uint8_t *slot_ctx = icc + xhci_ctx_size;
    xhci_build_slot_ctx(slot_ctx, xhci_dev.port, xhci_dev.speed,
                        xhci_dev.addr, 1);
    uint8_t *ep0_ctx = slot_ctx + xhci_ctx_size;
    uint64_t tr_deq = xhci_phys(xhci_dev.ctrl_ring.ring) | xhci_dev.ctrl_ring.cycle;
    xhci_build_ep0_ctx(ep0_ctx, max_packet, tr_deq);

    xhci_trb_t cmd = {0};
    cmd.parameter = xhci_phys(xhci_dev.input_ctx);
    cmd.control = (TRB_CMD_EVAL_CONTEXT << TRB_TYPE_SHIFT) | ((uint32_t)xhci_dev.slot << 24);
    return xhci_cmd(cmd, NULL);
}

static int xhci_update_address(uint8_t addr) {
    uint8_t *icc = (uint8_t *)xhci_dev.input_ctx;
    memset(icc, 0, xhci_ctx_size);
    *(uint32_t *)icc = 0;
    *(uint32_t *)(icc + 4) = (1u << 0);

    uint8_t *slot_ctx = icc + xhci_ctx_size;
    xhci_build_slot_ctx(slot_ctx, xhci_dev.port, xhci_dev.speed, addr, 1);
    xhci_trb_t cmd = {0};
    cmd.parameter = xhci_phys(xhci_dev.input_ctx);
    cmd.control = (TRB_CMD_EVAL_CONTEXT << TRB_TYPE_SHIFT) | ((uint32_t)xhci_dev.slot << 24);
    int rc = xhci_cmd(cmd, NULL);
    if (rc == 0)
        xhci_dev.addr = addr;
    return rc;
}

static int xhci_configure_interrupt_ep(uint8_t ep, uint16_t max_packet, uint8_t interval,
                                       xhci_intr_handle_t *handle) {
    uint8_t dci = (uint8_t)(ep * 2 + 1);
    xhci_dev.intr_dci = dci;

    uint8_t *icc = (uint8_t *)xhci_dev.input_ctx;
    memset(icc, 0, xhci_ctx_size);
    *(uint32_t *)icc = 0;
    *(uint32_t *)(icc + 4) = (1u << 0) | (1u << dci);

    uint8_t *slot_ctx = icc + xhci_ctx_size;
    uint8_t ctx_entries = dci;
    if (ctx_entries < 1)
        ctx_entries = 1;
    xhci_build_slot_ctx(slot_ctx, xhci_dev.port, xhci_dev.speed,
                        xhci_dev.addr, ctx_entries);

    uint8_t *ep_ctx = slot_ctx + xhci_ctx_size * dci;
    uint64_t tr_deq = xhci_phys(handle->ring.ring) | handle->ring.cycle;
    xhci_build_int_ctx(ep_ctx, max_packet, interval, tr_deq);

    xhci_trb_t cmd = {0};
    cmd.parameter = xhci_phys(xhci_dev.input_ctx);
    cmd.control = (TRB_CMD_CONFIGURE_ENDPOINT << TRB_TYPE_SHIFT) | ((uint32_t)xhci_dev.slot << 24);
    int rc = xhci_cmd(cmd, NULL);
    if (rc == 0)
        xhci_dev.intr_configured = 1;
    return rc;
}

static int xhci_setup_device(uint8_t port) {
    xhci_trb_t cmd = {0};
    cmd.control = (TRB_CMD_ENABLE_SLOT << TRB_TYPE_SHIFT);
    xhci_trb_t result = {0};
    if (xhci_cmd(cmd, &result) != 0) {
        serial_printf("[xHCI] enable slot failed for port %u\n", port);
        return -1;
    }
    uint8_t slot = (uint8_t)(result.control >> 24);
    if (slot == 0 || slot > xhci_max_slots) {
        serial_printf("[xHCI] invalid slot id %u\n", slot);
        return -1;
    }

    memset(&xhci_dev, 0, sizeof(xhci_dev));
    xhci_dev.slot = slot;
    xhci_dev.port = port;
    xhci_dev.speed = (uint8_t)((xhci_read32(xhci_op, XHCI_OP_PORTS + port * 0x10) & XHCI_PORT_SPEED_MASK) >> 10);
    xhci_dev.addr = 0;
    xhci_dev.max_packet = xhci_initial_mps(xhci_dev.speed);
    xhci_dev.ctrl_ring.ring = NULL;
    xhci_dev.ctrl_ring.size = 0;

    xhci_dev.dev_ctx = kmalloc_aligned(4096, 64);
    xhci_dev.input_ctx = kmalloc_aligned(4096, 64);
    if (!xhci_dev.dev_ctx || !xhci_dev.input_ctx) {
        serial_write_string("[xHCI] alloc failed for device contexts\n");
        return -1;
    }
    memset(xhci_dev.dev_ctx, 0, 4096);
    memset(xhci_dev.input_ctx, 0, 4096);

    xhci_dcbaa[slot] = xhci_phys(xhci_dev.dev_ctx);
    xhci_mmio_sync();

    xhci_ring_init(&xhci_dev.ctrl_ring, 256);

    uint8_t *icc = (uint8_t *)xhci_dev.input_ctx;
    memset(icc, 0, xhci_ctx_size);
    *(uint32_t *)icc = 0;
    *(uint32_t *)(icc + 4) = (1u << 0) | (1u << 1);

    uint8_t *slot_ctx = icc + xhci_ctx_size;
    xhci_build_slot_ctx(slot_ctx, port, xhci_dev.speed, 0, 1);

    uint8_t *ep0_ctx = slot_ctx + xhci_ctx_size;
    uint64_t tr_deq = xhci_phys(xhci_dev.ctrl_ring.ring) | xhci_dev.ctrl_ring.cycle;
    xhci_build_ep0_ctx(ep0_ctx, xhci_dev.max_packet, tr_deq);

    cmd.parameter = xhci_phys(xhci_dev.input_ctx);
    cmd.control = (TRB_CMD_ADDRESS_DEVICE << TRB_TYPE_SHIFT) |
                  ((uint32_t)slot << 24) | (1u << 9);
    if (xhci_cmd(cmd, &result) != 0) {
        serial_printf("[xHCI] address device failed for slot %u\n", slot);
        return -1;
    }

    xhci_dev.ready = 1;
    serial_printf("[xHCI] device on port %u ready (slot %u)\n", port, slot);
    return 0;
}

int xhci_init(uint32_t mmio_phys, volatile uint8_t *base) {
    (void)mmio_phys;
    if (!base)
        return -1;

    xhci_base = base;
    xhci_caplen = *(volatile uint8_t *)xhci_base;
    xhci_op = xhci_base + xhci_caplen;

    uint32_t hcsparams1 = xhci_read32(xhci_base, XHCI_CAP_HCSPARAMS1);
    xhci_max_slots = hcsparams1 & 0xFF;
    xhci_max_ports = (hcsparams1 >> 24) & 0xFF;

    uint32_t hccparams1 = xhci_read32(xhci_base, XHCI_CAP_HCCPARAMS1);
    xhci_ctx_size = ((hccparams1 >> 2) & 1) ? 64 : 32;

    uint32_t dboff = xhci_read32(xhci_base, XHCI_CAP_DBOFF) & ~0x3u;
    uint32_t rtsoff = xhci_read32(xhci_base, XHCI_CAP_RTSOFF) & ~0x1Fu;
    xhci_db = xhci_base + dboff;
    xhci_rt = xhci_base + rtsoff + 0x20;

    uint32_t cmd = xhci_read32(xhci_op, XHCI_OP_USBCMD);
    cmd &= ~XHCI_USBCMD_RUN;
    xhci_write32(xhci_op, XHCI_OP_USBCMD, cmd);
    xhci_mmio_sync();

    for (int i = 0; i < 1000; ++i) {
        if (xhci_read32(xhci_op, XHCI_OP_USBSTS) & XHCI_USBSTS_HCH)
            break;
        hpet_delay_ms(1);
    }

    cmd = xhci_read32(xhci_op, XHCI_OP_USBCMD);
    cmd |= XHCI_USBCMD_RESET;
    xhci_write32(xhci_op, XHCI_OP_USBCMD, cmd);
    xhci_mmio_sync();

    for (int i = 0; i < 1000; ++i) {
        uint32_t sts = xhci_read32(xhci_op, XHCI_OP_USBSTS);
        if ((xhci_read32(xhci_op, XHCI_OP_USBCMD) & XHCI_USBCMD_RESET) == 0 &&
            (sts & XHCI_USBSTS_CNR) == 0)
            break;
        hpet_delay_ms(1);
    }

    if ((xhci_read32(xhci_op, XHCI_OP_PAGESIZE) & 1u) == 0) {
        serial_write_string("[xHCI] unsupported page size\n");
        return -1;
    }

    xhci_dcbaa = (uint64_t *)kmalloc_aligned(4096, 64);
    if (!xhci_dcbaa)
        return -1;
    memset(xhci_dcbaa, 0, 4096);
    xhci_write64(xhci_op, XHCI_OP_DCBAAP, xhci_phys(xhci_dcbaa));

    xhci_ring_init(&xhci_cmd_ring, 256);
    uint64_t crcr = xhci_phys(xhci_cmd_ring.ring) | xhci_cmd_ring.cycle;
    xhci_write64(xhci_op, XHCI_OP_CRCR, crcr);

    xhci_evt_ring = (xhci_trb_t *)kmalloc_aligned(xhci_evt_size * sizeof(xhci_trb_t), 64);
    memset(xhci_evt_ring, 0, xhci_evt_size * sizeof(xhci_trb_t));
    xhci_erst = (xhci_erst_entry_t *)kmalloc_aligned(sizeof(xhci_erst_entry_t), 64);
    memset(xhci_erst, 0, sizeof(xhci_erst_entry_t));
    xhci_erst[0].addr = xhci_phys(xhci_evt_ring);
    xhci_erst[0].size = xhci_evt_size;

    xhci_write32((volatile uint8_t *)xhci_rt, XHCI_RT_IMAN, 0);
    xhci_write32((volatile uint8_t *)xhci_rt, XHCI_RT_IMOD, 0);
    xhci_write32((volatile uint8_t *)xhci_rt, XHCI_RT_ERSTSZ, 1);
    xhci_write64((volatile uint8_t *)xhci_rt, XHCI_RT_ERSTBA, xhci_phys(xhci_erst));
    xhci_write64((volatile uint8_t *)xhci_rt, XHCI_RT_ERDP, xhci_phys(xhci_evt_ring));

    xhci_write32(xhci_op, XHCI_OP_CONFIG, xhci_max_slots);

    cmd = xhci_read32(xhci_op, XHCI_OP_USBCMD);
    cmd |= XHCI_USBCMD_RUN | XHCI_USBCMD_INTE;
    xhci_write32(xhci_op, XHCI_OP_USBCMD, cmd);
    xhci_mmio_sync();

    for (int i = 0; i < 1000; ++i) {
        if ((xhci_read32(xhci_op, XHCI_OP_USBSTS) & XHCI_USBSTS_HCH) == 0)
            break;
        hpet_delay_ms(1);
    }

    usb_hc_ops_t ops = {
        .control_transfer = xhci_control_transfer,
        .interrupt_setup = xhci_interrupt_setup,
        .interrupt_poll = xhci_interrupt_poll
    };
    usb_register_hc(&ops);

    xhci_ready = 1;
    serial_write_string("[xHCI] initialized\n");
    return 0;
}

static int xhci_port_reset(uint8_t port) {
    uint32_t portsc = xhci_read32(xhci_op, XHCI_OP_PORTS + port * 0x10);
    portsc |= XHCI_PORTSC_PP;
    xhci_write32(xhci_op, XHCI_OP_PORTS + port * 0x10, portsc);
    xhci_mmio_sync();

    uint32_t change = XHCI_PORTSC_CSC | XHCI_PORTSC_PEC | XHCI_PORTSC_PRC | XHCI_PORTSC_WRC;
    xhci_write32(xhci_op, XHCI_OP_PORTS + port * 0x10, XHCI_PORTSC_PP | change);
    xhci_mmio_sync();

    uint32_t reset_bit = XHCI_PORTSC_PR;
    if (((portsc & XHCI_PORT_SPEED_MASK) >> 10) >= 4)
        reset_bit = XHCI_PORTSC_WPR;

    xhci_write32(xhci_op, XHCI_OP_PORTS + port * 0x10, XHCI_PORTSC_PP | reset_bit);
    xhci_mmio_sync();

    int timeout = 500;
    while (timeout-- > 0) {
        uint32_t cur = xhci_read32(xhci_op, XHCI_OP_PORTS + port * 0x10);
        if (cur & XHCI_PORTSC_PRC)
            break;
        hpet_delay_ms(1);
    }

    uint32_t cur = xhci_read32(xhci_op, XHCI_OP_PORTS + port * 0x10);
    if (cur & XHCI_PORTSC_PED) {
        xhci_write32(xhci_op, XHCI_OP_PORTS + port * 0x10, XHCI_PORTSC_PP | change);
        return 0;
    }
    return -1;
}

void xhci_poll_ports(void) {
    if (!xhci_ready)
        return;

    for (uint32_t p = 0; p < xhci_max_ports; ++p) {
        uint32_t portsc = xhci_read32(xhci_op, XHCI_OP_PORTS + p * 0x10);
        if (!(portsc & XHCI_PORTSC_CCS))
            continue;
        if (xhci_dev.ready && xhci_dev.port == p)
            continue;

        if (xhci_port_reset((uint8_t)p) == 0) {
            if (xhci_setup_device((uint8_t)p) == 0) {
                usb_attach_device((uint8_t)p);
            }
        }
    }
}

int xhci_control_transfer(uint8_t addr, uint8_t endp, void *setup, void *data,
                          uint16_t len) {
    (void)endp;
    if (!xhci_dev.ready || !setup)
        return -1;

    if (addr != xhci_dev.addr && addr != 0)
        return -1;

    usb_setup_pkt_t *req = (usb_setup_pkt_t *)setup;
    uint64_t setup_param = 0;
    memcpy(&setup_param, req, sizeof(usb_setup_pkt_t));

    uint32_t setup_type = SETUP_TT_NO_DATA;
    if (len > 0) {
        setup_type = (req->bmRequestType & 0x80) ? SETUP_TT_IN_DATA : SETUP_TT_OUT_DATA;
    }

    xhci_trb_t trb = {0};
    trb.parameter = setup_param;
    trb.status = 8;
    trb.control = (TRB_TYPE_SETUP << TRB_TYPE_SHIFT) | TRB_IDT | setup_type;
    xhci_ring_enqueue(&xhci_dev.ctrl_ring, trb);

    if (len && data) {
        trb = (xhci_trb_t){0};
        trb.parameter = xhci_phys(data);
        trb.status = len;
        trb.control = (TRB_TYPE_DATA << TRB_TYPE_SHIFT);
        if (req->bmRequestType & 0x80)
            trb.control |= TRB_DIR_IN;
        xhci_ring_enqueue(&xhci_dev.ctrl_ring, trb);
    }

    trb = (xhci_trb_t){0};
    trb.parameter = 0;
    trb.status = 0;
    trb.control = (TRB_TYPE_STATUS << TRB_TYPE_SHIFT) | TRB_IOC;
    if (!(req->bmRequestType & 0x80))
        trb.control |= TRB_DIR_IN;
    uint64_t status_ptr = xhci_ring_enqueue(&xhci_dev.ctrl_ring, trb);

    xhci_write32((volatile uint8_t *)xhci_db, xhci_dev.slot * 4, 1);

    int cc = xhci_wait_event(status_ptr, TRB_EVT_TRANSFER, NULL);
    if (cc != 1)
        return -1;

    if (req->bRequest == USB_REQ_SET_ADDRESS && addr == 0) {
        if (xhci_update_address((uint8_t)req->wValue) != 0)
            return -1;
    }

    if (req->bRequest == USB_REQ_GET_DESCRIPTOR &&
        ((req->wValue >> 8) == USB_DESC_DEVICE) &&
        req->wLength >= 8 && data && addr == 0) {
        uint8_t maxp = ((uint8_t *)data)[7];
        if (maxp && maxp != xhci_dev.max_packet) {
            xhci_dev.max_packet = maxp;
            xhci_update_ep0(maxp);
        }
    }

    return 0;
}

void *xhci_interrupt_setup(uint8_t addr, uint8_t endp, void *data,
                           uint16_t len) {
    if (!xhci_dev.ready || addr != xhci_dev.addr)
        return NULL;

    xhci_intr_handle_t *h = (xhci_intr_handle_t *)kmalloc(sizeof(*h));
    if (!h)
        return NULL;
    memset(h, 0, sizeof(*h));
    h->buffer = (uint8_t *)data;
    h->len = len;
    h->dci = (uint8_t)(endp * 2 + 1);

    xhci_ring_init(&h->ring, 64);

    if (!xhci_dev.intr_configured) {
        if (xhci_configure_interrupt_ep(endp, len, 8, h) != 0) {
            return NULL;
        }
    }

    xhci_trb_t trb = {0};
    trb.parameter = xhci_phys(h->buffer);
    trb.status = len;
    trb.control = (TRB_TYPE_NORMAL << TRB_TYPE_SHIFT) | TRB_IOC;
    h->pending_trb = xhci_ring_enqueue(&h->ring, trb);

    xhci_write32((volatile uint8_t *)xhci_db, xhci_dev.slot * 4, h->dci);
    return h;
}

int xhci_interrupt_poll(void *handle) {
    xhci_intr_handle_t *h = (xhci_intr_handle_t *)handle;
    if (!h)
        return 0;

    xhci_trb_t evt;
    int found = 0;
    while (xhci_event_dequeue(&evt)) {
        uint8_t type = (evt.control >> TRB_TYPE_SHIFT) & 0x3F;
        if (type != TRB_EVT_TRANSFER)
            continue;
        if (evt.parameter != h->pending_trb)
            continue;
        uint8_t cc = (evt.status >> 24) & 0xFF;
        if (cc == 1)
            found = 1;
        break;
    }

    if (found) {
        xhci_trb_t trb = {0};
        trb.parameter = xhci_phys(h->buffer);
        trb.status = h->len;
        trb.control = (TRB_TYPE_NORMAL << TRB_TYPE_SHIFT) | TRB_IOC;
        h->pending_trb = xhci_ring_enqueue(&h->ring, trb);
        xhci_write32((volatile uint8_t *)xhci_db, xhci_dev.slot * 4, h->dci);
        return 1;
    }
    return 0;
}
