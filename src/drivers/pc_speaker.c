#include "pc_speaker.h"
#include "../io.h"

//delay function (busy wait)
static void delay_ms(uint32_t ms) {
    //rough approximation timing
    for (uint32_t i = 0; i < ms * 1000; i++) {
        __asm__ volatile("nop");
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
    speaker_play_freq(frequency);
    delay_ms(duration_ms);
    speaker_stop();
}

void speaker_play_note(uint32_t note, uint32_t duration_ms) {
    speaker_beep(note, duration_ms);
}
