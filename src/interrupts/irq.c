#include "irq.h"
#include "pic.h"
#include <stddef.h>
#include <stdbool.h>
#include "../scheduler.h"
#include "../process.h"

//forward declaration for APIC support
extern bool apic_is_enabled(void);
extern void apic_send_eoi(void);

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

//irq_dispatch_with_context: called from modified IRQ stubs with stack pointer
//this allows us to capture interrupted context for preemptive scheduling
void irq_dispatch_with_context(int irq, uint32_t* stack_ptr) {
    if (irq >= 0 && irq < 16) {
        //count every dispatch
        irq_counts[irq]++;
        irq_handler_t h = irq_handlers[irq];
        if (h) {
            h();
        }
        
        //send EOI
        //for legacy PIC IRQs (1, 12), we need to send PIC EOI even with APIC
        //APIC timer (IRQ0) uses only APIC EOI
        if (apic_is_enabled()) {
            apic_send_eoi();
            //send PIC EOI for legacy device IRQs
            if (irq > 0) {
                pic_send_eoi((uint8_t)irq);
            }
        } else {
            pic_send_eoi((uint8_t)irq);
        }
        
        //check if preemption is needed after handling IRQ (especially timer IRQ 0)
        if (g_preempt_needed) {
            g_preempt_needed = 0;
            //capture interrupted process context from IRQ frame
            process_capture_irq_context(stack_ptr);
            //perform context switch now while still in IRQ context
            //this will switch to next process using the saved state
            schedule();
        }
    }
}

//legacy irq_dispatch for compatibility (non-preemptive path)
void irq_dispatch(int irq) {
    irq_dispatch_with_context(irq, NULL);
}

uint32_t irq_get_count(int irq) {
    if (irq < 0 || irq >= 16) return 0;
    return irq_counts[irq];
}

void irq_get_all_counts(uint32_t out_counts[16]) {
    if (!out_counts) return;
    for (int i = 0; i < 16; i++) out_counts[i] = irq_counts[i];
}
