#include "pic.h"
#include "../io.h"

void pic_remap(int offset1, int offset2) {
    uint8_t a1 = inb(PIC1_DATA);
    uint8_t a2 = inb(PIC2_DATA);

    //starts the initialization sequence
    outb(PIC1_COMMAND, 0x11);
    outb(PIC2_COMMAND, 0x11);

    //set vector offset
    outb(PIC1_DATA, offset1);
    outb(PIC2_DATA, offset2);

    //tell master PIC that there is a slave PIC at IRQ2
    outb(PIC1_DATA, 0x04);
    //tell slave PIC its cascade identity 
    outb(PIC2_DATA, 0x02);

    //8086/88 (MCS-80/85) mode
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    //restore saved masks
    outb(PIC1_DATA, a1);
    outb(PIC2_DATA, a2);
}

//send end of interrupt
void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}

//mask an interrupt
void pic_set_mask(uint8_t irq) {
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    value = inb(port) | (1 << irq);
    outb(port, value);
}

//unmask an interrupt
void pic_clear_mask(uint8_t irq) {
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    value = inb(port) & ~(1 << irq);
    outb(port, value);
}
