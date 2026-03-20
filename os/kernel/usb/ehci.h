#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int ehci_init(uint32_t mmio_phys);
int ehci_control_transfer(uint8_t addr, uint8_t endp, void* setup, void* data,
                          uint16_t len);
void* ehci_interrupt_setup(uint8_t addr, uint8_t endp, void* data,
                           uint16_t len);
int ehci_interrupt_poll(void* handle);
void ehci_poll_ports(void);

#ifdef __cplusplus
}
#endif
