#include "net.h"
#include "net_device.h"
#include "drivers/e1000.h"
#include "drivers/rtl8139.h"
#include "dhcp.h"

extern void serial(const char *fmt, ...);

void net_init(void) {
    serial("[NET] Initializing network subsystem...\n");

    /* Probe Drivers */
    if (e1000_init() == 0) {
        serial("[NET] E1000 driver loaded.\n");
        extern void sleep(uint32_t ms);
        sleep(200);      /* Give some time for Link to go UP */
        dhcp_discover(); /* Auto-configure */
        return;
    }

    if (rtl8139_init() == 0) {
        serial("[NET] RTL8139 driver loaded.\n");
        dhcp_discover(); /* Auto-configure */
        return;
    }

    serial("[NET] No network device found.\n");
}

void net_poll(void) {
    net_device_t* dev = net_get_primary_device();
    if (dev && dev->poll) dev->poll(dev);
}