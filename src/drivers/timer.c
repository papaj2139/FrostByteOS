#include "timer.h"
#include "../interrupts/irq.h"
#include "../interrupts/pic.h"
#include "../io.h"
#include "../scheduler.h"
#include <stdbool.h>

//forward declaration to avoid circular dependency
void process_timer_tick(void);

//forward declaration for APIC
extern bool apic_is_enabled(void);
extern uint32_t apic_timer_get_ticks(void);

#define PIT_FREQUENCY 1193180u

static volatile uint64_t g_ticks = 0;
static uint32_t g_hz = 0;
static void (*g_timer_cb)(void) = 0;

static void timer_irq_handler(void) {
    g_ticks++;
    
    //call process manager timer tick for scheduling
    scheduler_tick();
    // optional callback
    if (g_timer_cb) g_timer_cb();
}

void timer_init(uint32_t frequency) {
    if (frequency == 0) frequency = 100; //default
    g_hz = frequency;
    uint32_t divisor = PIT_FREQUENCY / frequency;

    //register IRQ0 handler and unmask IRQ0
    irq_install_handler(0, timer_irq_handler);
    pic_clear_mask(0);

    //command: channel 0 access lobyte/hibyte mode 3 (square wave) binary
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

uint64_t timer_get_ticks(void) {
    //return APIC ticks if APIC is enabled otherwise PIT ticks
    if (apic_is_enabled()) {
        return (uint64_t)apic_timer_get_ticks();
    }
    return g_ticks;
}

uint32_t timer_get_frequency(void) {
    return g_hz;
}

void timer_register_callback(void (*cb)(void)) {
    g_timer_cb = cb;
}
