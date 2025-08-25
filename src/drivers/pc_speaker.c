#include "pc_speaker.h"
#include "../io.h"
#include "timer.h"

//detect if maskable interrupts are enabled (IF flag)
static inline int interrupts_enabled(void) {
    uint32_t eflags;
    __asm__ volatile ("pushf; pop %0" : "=r"(eflags));
    return (eflags & (1u << 9)) != 0;
}

//sleep using PIT timer ticks CPU halts until next IRQ to save power
static void sleep_ms(uint32_t ms) {
    //fallback when interrupts are disabled (e.g., panic path)
    if (!interrupts_enabled()) {
        volatile uint32_t nops = ms * 1000u; //rough approximation
        for (volatile uint32_t i = 0; i < nops; ++i) {
            __asm__ volatile ("nop");
        }
        return;
    }

    uint32_t hz = timer_get_frequency();
    if (hz == 0) hz = 100; //fallback
    uint64_t start = timer_get_ticks();
    //compute ceil(ms*hz/1000)
    uint32_t whole = ms / 1000;
    uint32_t rem = ms % 1000;
    uint32_t ticks_needed32 = whole * hz + (uint32_t)(((uint32_t)rem * hz + 999) / 1000);
    uint64_t target = start + (uint64_t)ticks_needed32;
    while (timer_get_ticks() < target) {
        __asm__ volatile ("hlt");
    }
}

void speaker_init(void) {
    //initialize speaker ensure it off initially
    speaker_stop();
}

void speaker_play_freq(uint32_t frequency) {
    if (frequency == 0) {
        speaker_stop();
        return;
    }
    
    //calculate divisor for PIT
    uint32_t divisor = 1193180 / frequency;
    
    //configure PIT channel 2 for square wave
    outb(PIT_COMMAND, PIT_SPEAKER_CMD);
    
    //send divisor (low byte first then high byte)
    outb(PIT_CHANNEL_2, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL_2, (uint8_t)((divisor >> 8) & 0xFF));
    
    //enable speaker
    uint8_t speaker_reg = inb(SPEAKER_PORT);
    outb(SPEAKER_PORT, speaker_reg | 0x03);
}

void speaker_stop(void) {
    //disable speaker
    uint8_t speaker_reg = inb(SPEAKER_PORT);
    outb(SPEAKER_PORT, speaker_reg & 0xFC);
}

void speaker_beep(uint32_t frequency, uint32_t duration_ms) {
    if (frequency == 0 || duration_ms == 0) { speaker_stop(); return; }
    speaker_play_freq(frequency);

    if (!interrupts_enabled()) {
        //when IRQs are disabled use PIT channel 2 OUT (bit 5 of port 0x61) to count toggles and apprxoimate duration OUT toggles at 2*f
        uint32_t toggles_needed = (duration_ms * frequency + 499) / 500; //round
        uint8_t last = inb(SPEAKER_PORT) & 0x20;
        while (toggles_needed > 0) {
            uint8_t cur = inb(SPEAKER_PORT) & 0x20;
            if (cur != last) { last = cur; toggles_needed--; }
        }
    } else {
        sleep_ms(duration_ms);
    }
    speaker_stop();
}

void speaker_play_note(uint32_t note, uint32_t duration_ms) {
    speaker_beep(note, duration_ms);
}
