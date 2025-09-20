#include "timer.h"
#include "../interrupts/irq.h"
#include "../interrupts/pic.h"
#include "../io.h"

//forward declaration to avoid circular dependency
void process_timer_tick(void);

#define PIT_FREQUENCY 1193180u

static volatile uint64_t g_ticks = 0;
static uint32_t g_hz = 0;

static void timer_irq_handler(void) {
    g_ticks++;
    
    //call process manager timer tick for scheduling
    process_timer_tick();
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
    return g_ticks;
}

uint32_t timer_get_frequency(void) {
    return g_hz;
}
