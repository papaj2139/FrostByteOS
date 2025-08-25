#include "mouse.h"
#include "../io.h"
#include "../interrupts/irq.h"
#include "../interrupts/pic.h"

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
