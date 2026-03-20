#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* USB Request Types */
#define USB_RT_D2H      0x80 /* Device to Host */
#define USB_RT_H2D      0x00 /* Host to Device */
#define USB_RT_STANDARD 0x00
#define USB_RT_CLASS    0x20
#define USB_RT_DEVICE   0x00
#define USB_RT_INTF     0x01
#define USB_RT_EP       0x02

/* USB Requests */
#define USB_REQ_GET_STATUS        0x00
#define USB_REQ_SET_ADDRESS       0x05
#define USB_REQ_GET_DESCRIPTOR    0x06
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_REQ_SET_IDLE          0x0A /* HID specific */

/* Descriptor Types */
#define USB_DESC_DEVICE           0x01
#define USB_DESC_CONFIGURATION    0x02
#define USB_DESC_INTERFACE        0x04
#define USB_DESC_ENDPOINT         0x05
#define USB_DESC_HID              0x21

/* Structures */
typedef struct {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed)) usb_setup_pkt_t;

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} __attribute__((packed)) usb_dev_desc_t;

typedef struct {
    int (*control_transfer)(uint8_t addr, uint8_t endp, void* setup,
                            void* data, uint16_t len);
    void* (*interrupt_setup)(uint8_t addr, uint8_t endp, void* data,
                             uint16_t len);
    int (*interrupt_poll)(void* handle);
} usb_hc_ops_t;

/* Initializes the USB subsystem and scans for controllers */
void usb_core_init(void);

/* Called by HC driver when a device is attached to a port */
void usb_attach_device(uint8_t port_id);

/* Polls registered HID devices for input */
void usb_poll(void);

/* Host controller registration */
void usb_register_hc(const usb_hc_ops_t* ops);

/* HC generic helpers */
int usb_control_transfer(uint8_t addr, uint8_t endp, void* setup, void* data,
                         uint16_t len);
void* usb_interrupt_setup(uint8_t addr, uint8_t endp, void* data,
                          uint16_t len);
int usb_interrupt_poll(void* handle);

#ifdef __cplusplus
}
#endif
