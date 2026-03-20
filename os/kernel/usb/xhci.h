#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int xhci_init(uint32_t mmio_phys, volatile uint8_t *base);
void xhci_poll_ports(void);
int xhci_control_transfer(uint8_t addr, uint8_t endp, void *setup, void *data,
                          uint16_t len);
void *xhci_interrupt_setup(uint8_t addr, uint8_t endp, void *data,
                           uint16_t len);
int xhci_interrupt_poll(void *handle);

#ifdef __cplusplus
}
#endif
