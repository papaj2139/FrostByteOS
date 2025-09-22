#include "irq.h"
#include "pic.h"

static irq_handler_t irq_handlers[16] = {0};
static volatile uint32_t irq_counts[16] = {0};

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
        //count every dispatch
        irq_counts[irq]++;
        irq_handler_t h = irq_handlers[irq];
        if (h) {
            h();
        }
        pic_send_eoi((uint8_t)irq);
    }
}

uint32_t irq_get_count(int irq) {
    if (irq < 0 || irq >= 16) return 0;
    return irq_counts[irq];
}

void irq_get_all_counts(uint32_t out_counts[16]) {
    if (!out_counts) return;
    for (int i = 0; i < 16; i++) out_counts[i] = irq_counts[i];
}
