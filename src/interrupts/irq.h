#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>

typedef void (*irq_handler_t)(void);

void irq_install_handler(int irq, irq_handler_t handler);
void irq_uninstall_handler(int irq);

//called by assembly stubs
void irq_dispatch(int irq);

//read-only counters
uint32_t irq_get_count(int irq);
void irq_get_all_counts(uint32_t out_counts[16]);

#endif
