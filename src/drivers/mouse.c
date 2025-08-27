#include "mouse.h"
#include "../io.h"
#include "../interrupts/irq.h"
#include "../interrupts/pic.h"
#include "../device_manager.h"
#include <string.h>

//IRQ12-driven PS/2 mouse handler with packet ring buffer
typedef struct { 
    int8_t b[3]; 
} mouse_pkt_t;
static volatile mouse_pkt_t pktbuf[16];
static volatile uint8_t pkt_head = 0, pkt_tail = 0;

static inline int pkt_empty(void) { 
    return pkt_head == pkt_tail; 
}
static inline void pkt_push(int8_t b0, int8_t b1, int8_t b2) {
    uint8_t next = (uint8_t)((pkt_head + 1) & 15);
    if (next != pkt_tail) {
        pktbuf[pkt_head].b[0] = b0;
        pktbuf[pkt_head].b[1] = b1;
        pktbuf[pkt_head].b[2] = b2;
        pkt_head = next;
    }
}

static int8_t ib0, ib1, ib2; //in progress bytes
static uint8_t mcycle = 0;

static void mouse_wait(uint8_t type) {
    uint32_t timeout = 100000;
    if (type == 0) {
        while (timeout--) { if (inb(0x64) & 1) return; }
    } else {
        while (timeout--) { if ((inb(0x64) & 2) == 0) return; }
    }
}

static void mouse_write(uint8_t val) {
    mouse_wait(1); outb(0x64, 0xD4);
    mouse_wait(1); outb(0x60, val);
}

static uint8_t mouse_read(void) {
    mouse_wait(0);
    return inb(0x60);
}

static void mouse_irq_handler(void) {
    uint8_t data = inb(0x60); //IRQ12 guarantees this is mouse data
    if (mcycle == 0) {
        if (!(data & 0x08)) return; //resync first byte must have bit3 set
        ib0 = (int8_t)data; mcycle = 1; return;
    }
    if (mcycle == 1) { ib1 = (int8_t)data; mcycle = 2; return; }
    ib2 = (int8_t)data; mcycle = 0; pkt_push(ib0, ib1, ib2);
}

void mouse_init(void) {
    //enable auoxiliary device and its IRQ in controller command byte
    mouse_wait(1); outb(0x64, 0xA8);      //enable auxiliary device
    mouse_wait(1); outb(0x64, 0x20);      //get command byte
    mouse_wait(0); uint8_t status = inb(0x60) | 2; //enable IRQ12 in controller
    mouse_wait(1); outb(0x64, 0x60);      //set command byte
    mouse_wait(1); outb(0x60, status);

    //configure mouse
    mouse_write(0xF6); (void)mouse_read(); //set defaults
    mouse_write(0xF4); (void)mouse_read(); //enable data reporting (streaming)

    //install IRQ12handler unmask IRQ2 and IRQ12
    irq_install_handler(12, mouse_irq_handler);
    pic_clear_mask(2);   //cascade for slave PIC
    pic_clear_mask(12);  //mouse IRQ
}

int mouse_poll_packet(int8_t out_bytes[3]) {
    if (pkt_empty()) return 0;
    out_bytes[0] = pktbuf[pkt_tail].b[0];
    out_bytes[1] = pktbuf[pkt_tail].b[1];
    out_bytes[2] = pktbuf[pkt_tail].b[2];
    pkt_tail = (uint8_t)((pkt_tail + 1) & 15);
    return 1;
}

//device manager integration

//mouse device operations
static const device_ops_t mouse_ops = {
    .init = mouse_device_init,
    .read = mouse_device_read,
    .write = mouse_device_write,
    .ioctl = mouse_device_ioctl,
    .cleanup = mouse_device_cleanup
};

//muse device instance
static device_t mouse_device;

device_t* mouse_create_device(void) {
    //initialize device structure
    strcpy(mouse_device.name, "ps2mouse0");
    mouse_device.type = DEVICE_TYPE_INPUT;
    mouse_device.subtype = DEVICE_SUBTYPE_MOUSE;
    mouse_device.status = DEVICE_STATUS_UNINITIALIZED;
    mouse_device.device_id = 0; //will be assigned by device manager
    mouse_device.private_data = NULL;
    mouse_device.ops = &mouse_ops;
    mouse_device.next = NULL;
    
    return &mouse_device;
}

int mouse_device_init(device_t* device) {
    (void)device; //unused for now
    
    //mouse is already initialized by mouse_init()
    //just verify it's working
    return 0; //success
}

int mouse_device_read(device_t* device, uint32_t offset, void* buffer, uint32_t size) {
    (void)device; //unused
    (void)offset; //unused for mouse
    
    if (!buffer || size < 3) {
        return -1; //buffer too small for mouse packet
    }
    
    int8_t* packet_buffer = (int8_t*)buffer;
    
    //read available mouse packet
    if (mouse_poll_packet(packet_buffer)) {
        return 3; //return bytes read (3 bytes per mouse packet)
    }
    
    return 0; //no packet available
}

int mouse_device_write(device_t* device, uint32_t offset, const void* buffer, uint32_t size) {
    (void)device;
    (void)offset;
    (void)buffer;
    (void)size;
    
    //mouse is input-only device
    return -1;
}

int mouse_device_ioctl(device_t* device, uint32_t cmd, void* arg) {
    (void)device;
    
    //mouse-specific ioctl commands
    switch (cmd) {
        case 0x01: //MOUSE_IOCTL_GET_PACKET_COUNT
            if (arg) {
                int* count = (int*)arg;
                *count = (pkt_head - pkt_tail) & 15;
                return 0;
            }
            break;
        case 0x02: //MOUSE_IOCTL_FLUSH_BUFFER
            pkt_head = pkt_tail = 0;
            return 0;
        case 0x03: //MOUSE_IOCTL_GET_STATE
            //could return mouse state info
            return -1; //not implemented yet
        default:
            break;
    }
    
    return -1; //unknown command
}

void mouse_device_cleanup(device_t* device) {
    (void)device;
    //nothing to cleanup for mouse
}

int mouse_register_device(void) {
    //create mouse device
    device_t* mouse_dev = mouse_create_device();
    if (!mouse_dev) {
        return -1;
    }
    
    //register with device manager first
    if (device_register(mouse_dev) != 0) {
        mouse_device_cleanup(mouse_dev);
        return -1;
    }
    
    //then initialize through device manager
    if (device_init(mouse_dev) != 0) {
        //cleanup on failure
        device_unregister(mouse_dev->device_id);
        return -1;
    }
    
    return 0;
}