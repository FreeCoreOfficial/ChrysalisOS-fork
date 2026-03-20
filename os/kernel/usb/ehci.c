#include "ehci.h"
#include "usb_core.h"
#include "../drivers/serial.h"
#include "../mem/kmalloc.h"
#include "../string.h"
#include "../time/timer.h"
#include "../hardware/hpet.h"
#ifndef INSTALLER_BUILD
#include "../mm/vmm.h"
#include "../mm/paging.h"
#endif

#define EHCI_USBCMD     0x00
#define EHCI_USBSTS     0x04
#define EHCI_USBINTR    0x08
#define EHCI_FRINDEX    0x0C
#define EHCI_CTRLDSSEG  0x10
#define EHCI_PERIODIC   0x14
#define EHCI_ASYNC      0x18
#define EHCI_CONFIGFLAG 0x40
#define EHCI_PORTSC     0x44

#define EHCI_CMD_RUN        (1u << 0)
#define EHCI_CMD_HCRESET    (1u << 1)
#define EHCI_CMD_ASYNC_EN   (1u << 5)
#define EHCI_CMD_PERIOD_EN  (1u << 4)

#define EHCI_STS_HALTED     (1u << 12)

#define EHCI_PORT_CCS       (1u << 0)
#define EHCI_PORT_PED       (1u << 2)
#define EHCI_PORT_RESET     (1u << 8)
#define EHCI_PORT_OWNER     (1u << 13)

typedef struct __attribute__((packed)) {
  uint32_t next_qtd;
  uint32_t alt_next_qtd;
  uint32_t token;
  uint32_t buf[5];
} ehci_qtd_t;

typedef struct __attribute__((packed)) {
  uint32_t horiz_link;
  uint32_t ep_char;
  uint32_t ep_cap;
  uint32_t cur_qtd;
  uint32_t next_qtd;
  uint32_t alt_next_qtd;
  uint32_t token;
  uint32_t buf[5];
  uint32_t buf_hi[5];
} ehci_qh_t;

typedef struct {
  ehci_qh_t* qh;
  ehci_qtd_t* qtd;
  uint8_t* buf;
  uint16_t len;
} ehci_intr_handle_t;

static volatile uint8_t* ehci_base = NULL;
static uint8_t ehci_caplen = 0;
static uint32_t ehci_hcsparams = 0;
static uint32_t ehci_hccparams = 0;
static uint32_t* ehci_periodic = NULL;
static ehci_qh_t* ehci_async_qh = NULL;

static inline uint32_t ehci_read32(uint32_t off) {
  return *(volatile uint32_t *)(ehci_base + off);
}

static inline void ehci_write32(uint32_t off, uint32_t val) {
  *(volatile uint32_t *)(ehci_base + off) = val;
}

static uint32_t ehci_phys(void* ptr) {
#ifdef INSTALLER_BUILD
  return (uint32_t)(uintptr_t)ptr;
#else
  return vmm_virt_to_phys(ptr);
#endif
}

static void ehci_wait_halt(void) {
  for (int i = 0; i < 100000; ++i) {
    if (ehci_read32(ehci_caplen + EHCI_USBSTS) & EHCI_STS_HALTED)
      return;
  }
}

static void ehci_wait_run(void) {
  for (int i = 0; i < 100000; ++i) {
    if ((ehci_read32(ehci_caplen + EHCI_USBSTS) & EHCI_STS_HALTED) == 0)
      return;
  }
}

static void ehci_reset_controller(void) {
  uint32_t cmd = ehci_read32(ehci_caplen + EHCI_USBCMD);
  cmd &= ~EHCI_CMD_RUN;
  ehci_write32(ehci_caplen + EHCI_USBCMD, cmd);
  ehci_wait_halt();

  cmd = ehci_read32(ehci_caplen + EHCI_USBCMD);
  cmd |= EHCI_CMD_HCRESET;
  ehci_write32(ehci_caplen + EHCI_USBCMD, cmd);
  while (ehci_read32(ehci_caplen + EHCI_USBCMD) & EHCI_CMD_HCRESET) {
    asm volatile("pause");
  }
}

static void ehci_setup_async(void) {
  ehci_async_qh = (ehci_qh_t*)kmalloc_aligned(sizeof(ehci_qh_t), 32);
  memset(ehci_async_qh, 0, sizeof(ehci_qh_t));
  uint32_t qh_phys = ehci_phys(ehci_async_qh);
  ehci_async_qh->horiz_link = qh_phys | 0x2; /* QH type */
  ehci_async_qh->ep_char = (1u << 15); /* H bit */
  ehci_async_qh->next_qtd = 1;
  ehci_async_qh->alt_next_qtd = 1;
  ehci_write32(ehci_caplen + EHCI_ASYNC, qh_phys);

  uint32_t cmd = ehci_read32(ehci_caplen + EHCI_USBCMD);
  cmd |= EHCI_CMD_ASYNC_EN | EHCI_CMD_RUN;
  ehci_write32(ehci_caplen + EHCI_USBCMD, cmd);
  ehci_wait_run();
}

static void ehci_setup_periodic(void) {
  ehci_periodic = (uint32_t*)kmalloc_aligned(1024 * 4, 4096);
  memset(ehci_periodic, 0, 1024 * 4);
  for (int i = 0; i < 1024; ++i)
    ehci_periodic[i] = 1;
  ehci_write32(ehci_caplen + EHCI_PERIODIC, ehci_phys(ehci_periodic));

  uint32_t cmd = ehci_read32(ehci_caplen + EHCI_USBCMD);
  cmd |= EHCI_CMD_PERIOD_EN | EHCI_CMD_RUN;
  ehci_write32(ehci_caplen + EHCI_USBCMD, cmd);
  ehci_wait_run();
}

int ehci_init(uint32_t mmio_phys) {
  if (!mmio_phys)
    return -1;
#ifdef INSTALLER_BUILD
  ehci_base = (volatile uint8_t*)(uintptr_t)mmio_phys;
#else
  ehci_base = (volatile uint8_t*)mmio_phys;
#endif
  ehci_caplen = *(volatile uint8_t*)(ehci_base + 0x00);
  ehci_hcsparams = *(volatile uint32_t*)(ehci_base + 0x04);
  ehci_hccparams = *(volatile uint32_t*)(ehci_base + 0x08);
  (void)ehci_hccparams;

  ehci_reset_controller();
  ehci_write32(ehci_caplen + EHCI_USBINTR, 0);
  ehci_write32(ehci_caplen + EHCI_CTRLDSSEG, 0);

  ehci_setup_async();
  ehci_setup_periodic();

  ehci_write32(ehci_caplen + EHCI_CONFIGFLAG, 1);

  usb_hc_ops_t ops = {
    .control_transfer = ehci_control_transfer,
    .interrupt_setup = ehci_interrupt_setup,
    .interrupt_poll = ehci_interrupt_poll
  };
  usb_register_hc(&ops);
  serial_write_string("[EHCI] initialized\n");
  return 0;
}

static void ehci_qtd_init(ehci_qtd_t* qtd, uint8_t pid, uint16_t len,
                          uint8_t toggle, void* buf) {
  memset(qtd, 0, sizeof(*qtd));
  qtd->next_qtd = 1;
  qtd->alt_next_qtd = 1;
  uint32_t token = 0;
  token |= (1u << 7); /* active */
  token |= (3u << 10); /* CERR */
  token |= ((uint32_t)len << 16);
  token |= (toggle ? (1u << 31) : 0);
  token |= ((uint32_t)pid << 8);
  qtd->token = token;
  if (buf) {
    uint32_t p = ehci_phys(buf);
    qtd->buf[0] = p;
    for (int i = 1; i < 5; ++i)
      qtd->buf[i] = (p & ~0xFFFu) + (i * 0x1000u);
  }
}

int ehci_control_transfer(uint8_t addr, uint8_t endp, void* setup, void* data,
                          uint16_t len) {
  if (!ehci_async_qh || !setup)
    return -1;

  ehci_qtd_t* td_setup = (ehci_qtd_t*)kmalloc_aligned(sizeof(ehci_qtd_t), 32);
  ehci_qtd_t* td_data = NULL;
  ehci_qtd_t* td_status = (ehci_qtd_t*)kmalloc_aligned(sizeof(ehci_qtd_t), 32);
  if (!td_setup || !td_status)
    return -1;

  ehci_qtd_init(td_setup, 0x2D, 8, 0, setup); /* SETUP */

  if (len && data) {
    td_data = (ehci_qtd_t*)kmalloc_aligned(sizeof(ehci_qtd_t), 32);
    if (!td_data)
      return -1;
    uint8_t pid = (*(uint8_t*)setup & 0x80) ? 0x69 : 0xE1; /* IN/OUT */
    ehci_qtd_init(td_data, pid, len, 1, data);
  }

  uint8_t status_pid = (len && data && ((*(uint8_t*)setup) & 0x80)) ? 0xE1 : 0x69;
  ehci_qtd_init(td_status, status_pid, 0, 1, NULL);

  if (td_data) {
    td_setup->next_qtd = ehci_phys(td_data);
    td_data->next_qtd = ehci_phys(td_status);
  } else {
    td_setup->next_qtd = ehci_phys(td_status);
  }

  ehci_async_qh->ep_char = (addr & 0x7F) | (endp << 8) | (2u << 12) | (64u << 16);
  ehci_async_qh->ep_cap = (1u << 30);
  ehci_async_qh->cur_qtd = 0;
  ehci_async_qh->next_qtd = ehci_phys(td_setup);
  ehci_async_qh->alt_next_qtd = 1;

  int timeout = 1000;
  while (timeout-- > 0) {
    if ((td_status->token & (1u << 7)) == 0)
      break;
    hpet_delay_ms(1);
  }

  int ok = (td_status->token & (1u << 7)) == 0;
  kfree(td_setup);
  if (td_data) kfree(td_data);
  kfree(td_status);
  return ok ? 0 : -1;
}

void* ehci_interrupt_setup(uint8_t addr, uint8_t endp, void* data,
                           uint16_t len) {
  ehci_intr_handle_t* h = (ehci_intr_handle_t*)kmalloc(sizeof(*h));
  if (!h)
    return NULL;
  memset(h, 0, sizeof(*h));
  h->qh = (ehci_qh_t*)kmalloc_aligned(sizeof(ehci_qh_t), 32);
  h->qtd = (ehci_qtd_t*)kmalloc_aligned(sizeof(ehci_qtd_t), 32);
  if (!h->qh || !h->qtd)
    return NULL;
  memset(h->qh, 0, sizeof(*h->qh));
  ehci_qtd_init(h->qtd, 0x69, len, 1, data); /* IN */
  h->qh->horiz_link = ehci_phys(h->qh) | 0x2;
  h->qh->ep_char = (addr & 0x7F) | (endp << 8) | (2u << 12) | (64u << 16);
  h->qh->ep_cap = (1u << 30);
  h->qh->next_qtd = ehci_phys(h->qtd);
  h->qh->alt_next_qtd = 1;
  h->buf = (uint8_t*)data;
  h->len = len;

  uint32_t qh_phys = ehci_phys(h->qh);
  for (int i = 0; i < 1024; ++i)
    ehci_periodic[i] = qh_phys | 0x2;

  return h;
}

int ehci_interrupt_poll(void* handle) {
  ehci_intr_handle_t* h = (ehci_intr_handle_t*)handle;
  if (!h || !h->qtd)
    return 0;
  if (h->qtd->token & (1u << 7))
    return 0;
  ehci_qtd_init(h->qtd, 0x69, h->len, 1, h->buf);
  h->qh->next_qtd = ehci_phys(h->qtd);
  return 1;
}

void ehci_poll_ports(void) {
  if (!ehci_base)
    return;
  uint32_t n_ports = ehci_hcsparams & 0x0F;
  for (uint32_t i = 0; i < n_ports; ++i) {
    uint32_t portsc = ehci_read32(ehci_caplen + EHCI_PORTSC + i * 4);
    if (portsc & EHCI_PORT_CCS) {
      if (portsc & EHCI_PORT_OWNER)
        continue;
      if (!(portsc & EHCI_PORT_PED)) {
        ehci_write32(ehci_caplen + EHCI_PORTSC + i * 4,
                     portsc | EHCI_PORT_RESET);
        hpet_delay_ms(50);
        portsc = ehci_read32(ehci_caplen + EHCI_PORTSC + i * 4);
        ehci_write32(ehci_caplen + EHCI_PORTSC + i * 4,
                     portsc & ~EHCI_PORT_RESET);
        hpet_delay_ms(10);
      }
      usb_attach_device((uint8_t)(i + 1));
    }
  }
}
