#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>

typedef void (*irq_handler_t)(void);

void irq_install_handler(int irq, irq_handler_t handler);
void irq_uninstall_handler(int irq);

//called by assembly stubs
void irq_dispatch(int irq);

#endif
