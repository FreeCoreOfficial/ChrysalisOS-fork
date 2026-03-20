#include "uhci.h"
#include "usb_core.h"
#include "../drivers/serial.h"
#include "../arch/i386/io.h"
#include "../interrupts/irq.h"
#include "../mem/kmalloc.h"
#include "../hardware/hpet.h"
#include "../string.h"

/* UHCI I/O Registers */
#define USBCMD      0x00    /* USB Command */
#define USBSTS      0x02    /* USB Status */
#define USBINTR     0x04    /* USB Interrupt Enable */
#define FRNUM       0x06    /* Frame Number */
#define FLBASEADD   0x08    /* Frame List Base Address */
#define SOFMOD      0x0C    /* Start of Frame Modify */
#define PORTSC1     0x10    /* Port 1 Status/Control */
#define PORTSC2     0x12    /* Port 2 Status/Control */

/* USBCMD Bits */
#define CMD_RS      (1 << 0) /* Run/Stop */
#define CMD_HCRESET (1 << 1) /* Host Controller Reset */
#define CMD_GRESET  (1 << 2) /* Global Reset */
#define CMD_MAXP    (1 << 7) /* Max Packet (0=32, 1=64) */

/* USBSTS Bits */
#define STS_USBINT  (1 << 0) /* USB Interrupt */
#define STS_ERROR   (1 << 1) /* USB Error */
#define STS_RD      (1 << 2) /* Resume Detect */
#define STS_HSE     (1 << 3) /* Host System Error */
#define STS_HCPE    (1 << 4) /* Host Controller Process Error */
#define STS_HCH     (1 << 5) /* HCHalted */

/* PORTSC Bits */
#define PORTSC_CCS  (1 << 0) /* Current Connect Status */
#define PORTSC_CSC  (1 << 1) /* Connect Status Change */
#define PORTSC_PE   (1 << 2) /* Port Enable */
#define PORTSC_PEC  (1 << 3) /* Port Enable Change */
#define PORTSC_LS   (1 << 8) /* Low Speed Device */
#define PORTSC_PR   (1 << 9) /* Port Reset */

/* TD Control/Status Bits */
#define TD_CS_ACTIVE    (1 << 23)
#define TD_CS_DAT0      (0 << 19)
#define TD_CS_DAT1      (1 << 19)
#define TD_CS_LS        (1 << 26) /* Low Speed */

/* Frame List Definitions */
#define FRAME_LIST_ENTRIES  1024
#define FRAME_PTR_TERM      (1 << 0) /* Terminate */

/* Kernel Memory Layout (from paging.c) */
#define KERNEL_VIRT_BASE    0xC0000000U
#define KERNEL_PHYS_BASE    0x00100000U

/* TD/QH Structures */
typedef struct {
    uint32_t link;
    uint32_t ctrl_status;
    uint32_t token;
    uint32_t buffer;
    uint32_t reserved[4];
} __attribute__((packed)) uhci_td_t;

typedef struct {
    uint32_t head_link;
    uint32_t element_link;
    uint32_t reserved[2];
} __attribute__((packed)) uhci_qh_t;

/* Packet Types (PID) */
#define PID_SETUP   0x2D
#define PID_IN      0x69
#define PID_OUT     0xE1

static uint32_t uhci_io_base = 0;
static uint32_t* frame_list_virt = 0;
static uhci_qh_t* qh_control = 0;

/* Helper to convert virtual kernel address to physical */
static uint32_t virt_to_phys(void* virt) {
    uint32_t v = (uint32_t)virt;
    if (v >= KERNEL_VIRT_BASE) {
        return (v - KERNEL_VIRT_BASE) + KERNEL_PHYS_BASE;
    }
    return v; /* Identity mapped (e.g. if kmalloc returns low mem) */
}

static void uhci_reset_port(uint16_t port) {
    uint16_t status = inw(uhci_io_base + port);
    /* Set Port Reset */
    outw(uhci_io_base + port, status | PORTSC_PR);
    
    hpet_delay_ms(50);
    
    /* Clear Port Reset */
    status = inw(uhci_io_base + port);
    outw(uhci_io_base + port, status & ~PORTSC_PR);
    
    /* Recovery time */
    hpet_delay_ms(10);
    
    /* Enable Port if not enabled */
    status = inw(uhci_io_base + port);
    if (!(status & PORTSC_PE)) {
        outw(uhci_io_base + port, status | PORTSC_PE);
        hpet_delay_ms(10);
    }
}

static void uhci_check_ports() {
    uint16_t ports[] = {PORTSC1, PORTSC2};
    for (int i = 0; i < 2; i++) {
        uint16_t port = ports[i];
        uint16_t status = inw(uhci_io_base + port);
        
        /* Handle Connect Status Change */
        if (status & PORTSC_CSC) {
            /* Clear change bit (Write 1 to clear) */
            outw(uhci_io_base + port, status);
            
            if (!(status & PORTSC_CCS)) {
                serial_printf("[UHCI] port %d disconnected\n", i + 1);
                continue;
            }
        }
        
        /* If connected but not enabled, reset and enable */
        if ((status & PORTSC_CCS) && !(status & PORTSC_PE)) {
            serial_printf("[UHCI] port %d connected\n", i + 1);
            
            uhci_reset_port(port);
            serial_printf("[UHCI] port %d reset\n", i + 1);
            
            status = inw(uhci_io_base + port);
            if (status & PORTSC_PE) {
                serial_printf("[UHCI] port %d enabled\n", i + 1);
                usb_attach_device(i + 1);
            }
        }
    }
}

/* UHCI Interrupt Handler */
static void uhci_handler(registers_t* regs) {
    (void)regs;
    
    uint16_t status = inw(uhci_io_base + USBSTS);
    
    if (status & STS_USBINT) {
        serial_write_string("[UHCI] IRQ received (USBINT)\r\n");
    }
    if (status & STS_RD) {
        /* Resume Detect / Port Change */
        uhci_check_ports();
    }
    if (status & STS_ERROR) {
        serial_write_string("[UHCI] IRQ received (ERROR)\r\n");
    }
    if (status & STS_HSE) {
        serial_write_string("[UHCI] IRQ received (HOST SYSTEM ERROR)\r\n");
    }
    if (status & STS_HCPE) {
        serial_write_string("[UHCI] IRQ received (PROCESS ERROR)\r\n");
    }

    /* Acknowledge all interrupts by writing 1s back */
    outw(uhci_io_base + USBSTS, status);
}

void uhci_init(uint32_t io_base, uint8_t irq) {
    uhci_io_base = io_base;

    /* 1. Register Interrupt Handler */
    irq_install_handler(irq, uhci_handler);

    /* 2. Reset Controller */
    /* Global Reset */
    outw(uhci_io_base + USBCMD, CMD_GRESET);
    /* Wait 10ms (approximate busy loop) */
    for (volatile int i = 0; i < 100000; i++);
    outw(uhci_io_base + USBCMD, 0);
    
    /* Host Controller Reset */
    outw(uhci_io_base + USBCMD, CMD_HCRESET);
    /* Wait for reset to complete */
    while (inw(uhci_io_base + USBCMD) & CMD_HCRESET);

    serial_write_string("[UHCI] reset\r\n");

    /* 3. Allocate Frame List (4KB aligned) */
    /* We need 1024 entries * 4 bytes = 4096 bytes */
    frame_list_virt = (uint32_t*)kmalloc_aligned(4096, 4096);
    if (!frame_list_virt) {
        serial_write_string("[UHCI] Failed to allocate frame list\r\n");
        return;
    }

    /* Initialize Frame List to invalid/terminate */
    for (int i = 0; i < FRAME_LIST_ENTRIES; i++) {
        frame_list_virt[i] = FRAME_PTR_TERM;
    }

    serial_write_string("[UHCI] frame list allocated\r\n");

    /* 3b. Allocate Control QH */
    /* Align to 64 bytes to ensure bits 4-5 are 0, avoiding invalid pointer interpretation */
    qh_control = (uhci_qh_t*)kmalloc_aligned(sizeof(uhci_qh_t), 64);
    memset(qh_control, 0, sizeof(uhci_qh_t));
    qh_control->head_link = 1; /* Terminate */
    qh_control->element_link = 1; /* Terminate */
    
    uint32_t qh_phys = virt_to_phys(qh_control);
    
    /* Link all frame list entries to this QH */
    for (int i = 0; i < FRAME_LIST_ENTRIES; i++) {
        frame_list_virt[i] = (qh_phys & 0xFFFFFFF0) | 0x2; /* QH select, ensure lower 4 bits are clean */
    }
    serial_write_string("[USB] default control pipe ready\r\n");

    /* 4. Set Frame List Base Address */
    uint32_t frame_list_phys = virt_to_phys(frame_list_virt);
    outl(uhci_io_base + FLBASEADD, frame_list_phys);

    /* 5. Enable Interrupts */
    /* Enable USB Interrupt (IOC), Error, Resume, Short Packet */
    outw(uhci_io_base + USBINTR, 0x000F);

    /* 6. Start Controller */
    /* Set Run/Stop bit */
    outw(uhci_io_base + USBCMD, CMD_RS | CMD_MAXP); // Run + 64-byte packets

    serial_write_string("[UHCI] controller running\r\n");

    /* Debug: Check status */
    uint16_t status = inw(uhci_io_base + USBSTS);
    if (!(status & STS_HCH)) {
        serial_write_string("[UHCI] Status: Running\r\n");
    } else {
        serial_write_string("[UHCI] Status: Halted (Error?)\r\n");
    }
    
    /* Check ports for initial connection */
    uhci_check_ports();

    /* Register UHCI ops */
    usb_hc_ops_t ops = {
        .control_transfer = uhci_control_transfer,
        .interrupt_setup = uhci_setup_interrupt,
        .interrupt_poll = uhci_poll_interrupt
    };
    usb_register_hc(&ops);
    
    /* Force an interrupt for testing (optional, but good for verification) */
    /* We can't easily force an IRQ without a transaction, but we can verify
       the controller is alive by checking the Frame Number incrementing. */
    /*
    uint16_t frnum = inw(uhci_io_base + FRNUM);
    serial_printf("[UHCI] Frame Number: %d\n", frnum);
    */
}

/* --- Transfer Implementation --- */

/* Helper to create a TD */
static uhci_td_t* uhci_create_td(uint32_t link, uint32_t token, void* buffer) {
    /* Align to 64 bytes to ensure bits 4-5 are 0 */
    uhci_td_t* td = (uhci_td_t*)kmalloc_aligned(sizeof(uhci_td_t), 64);
    if (!td) return 0;
    memset(td, 0, sizeof(uhci_td_t));
    td->link = link;
    td->ctrl_status = TD_CS_ACTIVE | (3 << 27); /* Active, 3 errors max */
    
    /* Check for Low Speed on Port 1 or 2 (Simplification: assuming device on port 1/2 matches) */
    /* In a real driver, we'd track speed per device address. For now, check ports. */
    uint16_t p1 = inw(uhci_io_base + PORTSC1);
    uint16_t p2 = inw(uhci_io_base + PORTSC2);
    if ((p1 & PORTSC_LS) || (p2 & PORTSC_LS)) {
        td->ctrl_status |= TD_CS_LS;
    }

    td->token = token;
    td->buffer = buffer ? virt_to_phys(buffer) : 0;
    return td;
}

int uhci_control_transfer(uint8_t addr, uint8_t endp, void* setup, void* data, uint16_t len) {
    /* 1. Setup Stage */
    /* PID=SETUP, len=7 (8 bytes), addr, endp, toggle=0 */
    uint32_t token_setup = (7 << 21) | (0 << 19) | (endp << 15) | (addr << 8) | PID_SETUP;
    uhci_td_t* td_setup = uhci_create_td(1, token_setup, setup);

    uhci_td_t* last_td = td_setup;
    
    /* Track TDs to free later */
    #define MAX_TDS 32
    uhci_td_t* tds[MAX_TDS];
    int td_count = 0;
    tds[td_count++] = td_setup;
    
    /* 2. Data Stage (Packetized) */
    int toggle = 1; /* Data stage starts with DATA1 */
    uint8_t* data_ptr = (uint8_t*)data;
    int remaining = len;

    if (len > 0 && data) {
        uint8_t req_type = *(uint8_t*)setup;
        uint8_t pid = (req_type & 0x80) ? PID_IN : PID_OUT;
        
        while (remaining > 0) {
            /* EP0 Max Packet Size is 8 bytes by default */
            int pkt_len = (remaining > 8) ? 8 : remaining;
            
            uint32_t token_data = ((pkt_len - 1) << 21) | (toggle ? TD_CS_DAT1 : TD_CS_DAT0) | (endp << 15) | (addr << 8) | pid;
            uhci_td_t* td = uhci_create_td(1, token_data, data_ptr);
            
            if (td_count < MAX_TDS) tds[td_count++] = td;
            
            /* Link previous to this */
            /* FIX: Set Bit 2 (VF) for Depth First, and ensure no other bits are set */
            last_td->link = (virt_to_phys(td) & 0xFFFFFFF0) | 0x4; 
            last_td = td;
            
            data_ptr += pkt_len;
            remaining -= pkt_len;
            toggle = !toggle;
        }
    }

    /* 3. Status Stage */
    /* Opposite direction of Data. If no Data, IN. */
    uint8_t status_pid = PID_IN;
    if (len > 0) {
        uint8_t req_type = *(uint8_t*)setup;
        if (req_type & 0x80) status_pid = PID_OUT;
    }
    
    /* Toggle is always 1 for Status stage */
    uint32_t token_status = (0x7FF << 21) | TD_CS_DAT1 | (endp << 15) | (addr << 8) | status_pid;
    uhci_td_t* td_status = uhci_create_td(1, token_status, 0);
    if (td_count < MAX_TDS) tds[td_count++] = td_status;
    
    /* Link Last -> Status */
    last_td->link = (virt_to_phys(td_status) & 0xFFFFFFF0) | 0x4;
    td_status->link = 1; /* Terminate the final TD */
    
    /* 4. Execute */
    /* Link chain to QH */
    /* FIX: Use head_link (offset 0) for TD chain, Terminate element_link */
    qh_control->element_link = 1;
    qh_control->head_link = virt_to_phys(td_setup);
    
    /* DEBUG: Verify Schedule */
    uint32_t flbase = inl(uhci_io_base + FLBASEADD);
    uint32_t fr0 = frame_list_virt[0];
    
    serial_printf("[UHCI] FLBASEADD = %x\n", flbase);
    serial_printf("[UHCI] frame[0] = %x\n", fr0);
    serial_printf("[UHCI] QH head = %x\n", qh_control->head_link);
    serial_printf("[UHCI] TD setup link = %x\n", td_setup->link);

    /* Wait for SETUP completion */
    int timeout = 1000;
    while ((td_setup->ctrl_status & TD_CS_ACTIVE) && timeout > 0) {
        hpet_delay_ms(1);
        timeout--;
    }
    if (td_setup->ctrl_status & TD_CS_ACTIVE) 
        serial_write_string("[UHCI] SETUP TD timeout\r\n");
    else 
        serial_write_string("[UHCI] SETUP TD executed\r\n");

    /* Wait for DATA completion (if any) */
    if (len > 0 && td_count >= 2) {
        /* Check the last data TD (index count-2, as status is count-1) */
        uhci_td_t* last_data_td = tds[td_count - 2];
        timeout = 1000;
        while ((last_data_td->ctrl_status & TD_CS_ACTIVE) && timeout > 0) {
            hpet_delay_ms(1);
            timeout--;
        }
        if (last_data_td->ctrl_status & TD_CS_ACTIVE)
            serial_write_string("[UHCI] DATA TD timeout\r\n");
        else
            serial_write_string("[UHCI] DATA TD completed\r\n");
    }

    /* Wait for STATUS completion */
    timeout = 1000;
    while ((td_status->ctrl_status & TD_CS_ACTIVE) && timeout > 0) {
        hpet_delay_ms(1);
        timeout--;
    }
    
    int ret = 0;
    if (td_status->ctrl_status & TD_CS_ACTIVE) {
        serial_write_string("[UHCI] STATUS TD timeout\r\n");
        ret = -1;
    } else {
        serial_write_string("[UHCI] STATUS TD completed\r\n");
    }

    if (td_status->ctrl_status & 0x7E0000) { /* Any error bits */
        serial_write_string("[UHCI] Control transfer error\r\n");
        ret = -2;
    }

    /* Clear QH */
    qh_control->head_link = 1; /* Terminate */

    /* Cleanup */
    for (int i = 0; i < td_count; i++) {
        kfree(tds[i]);
    }
    
    return ret;
}

void* uhci_setup_interrupt(uint8_t addr, uint8_t endp, void* data, uint16_t len) {
    /* Create a single TD for IN transfer */
    /* Toggle 0 initially? HID usually expects data toggle sync, but for simple polling 
       we might get away with 0 or 1. Let's try DAT0. */
    uint32_t token = ((len - 1) << 21) | TD_CS_DAT0 | (endp << 15) | (addr << 8) | PID_IN;
    uhci_td_t* td = uhci_create_td(1, token, data);
    
    /* Insert into schedule. For simplicity, link to QH Control (it's processed every frame) 
       or create a new QH. Let's reuse QH Control for now as we are single threaded. 
       Actually, better to append to the frame list directly or a separate QH. 
       Let's just link it to the QH Control element link when we poll? 
       No, "Polling-based" means we check it. 
       Let's insert it into the QH Control chain permanently? No, that blocks control transfers.
       
       Strategy: We won't link it permanently. uhci_poll_interrupt will link it if not active?
       Actually, simpler: Just create the TD. uhci_poll_interrupt will check status. 
       If active, it means it's running. If inactive, data is there.
       BUT it needs to be in the schedule to run.
       
       Let's link it to the frame list index 0 (which points to QH). 
       We can insert a new QH for interrupts. */
       
    /* For this stage, let's just return the TD. uhci_poll_interrupt will execute it synchronously-ish 
       or we link it to a dedicated Interrupt QH. */
       
    /* HACK for Stage 8: We will execute it on demand in uhci_poll_interrupt 
       by linking it to qh_control momentarily. */
    return td;
}

int uhci_poll_interrupt(void* td_handle) {
    uhci_td_t* td = (uhci_td_t*)td_handle;
    
    /* If active, it hasn't completed yet. */
    if (td->ctrl_status & TD_CS_ACTIVE) {
        /* It's running. Is it linked? If not, link it. */
        if (qh_control->head_link & 1) {
             qh_control->head_link = virt_to_phys(td);
        }
        return 0;
    }
    
    /* Not active -> Completed */
    /* Unlink */
    qh_control->head_link = 1;
    
    /* Check errors */
    /* Preserve LS bit and Data Toggle (bit 19) from previous state */
    uint32_t preserved = td->ctrl_status & (TD_CS_LS | (1 << 19));

    if (td->ctrl_status & 0x7E0000) {
        /* Error, reset */
        td->ctrl_status = TD_CS_ACTIVE | (3 << 27) | preserved;
        return 0;
    }
    
    /* Success! Data is in buffer. Reactivate for next poll. */
    td->ctrl_status = TD_CS_ACTIVE | (3 << 27) | preserved;
    
    return 1;
}
