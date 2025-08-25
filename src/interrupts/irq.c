#include "irq.h"
#include "pic.h"

static irq_handler_t irq_handlers[16] = {0};

void irq_install_handler(int irq, irq_handler_t handler) {
    if (irq >= 0 && irq < 16) {
        irq_handlers[irq] = handler;
    }
}

void irq_uninstall_handler(int irq) {
    if (irq >= 0 && irq < 16) {
        irq_handlers[irq] = 0;
    }
}

void irq_dispatch(int irq) {
    if (irq >= 0 && irq < 16) {
        irq_handler_t h = irq_handlers[irq];
        if (h) {
            h();
        }
        pic_send_eoi((uint8_t)irq);
    }
}
