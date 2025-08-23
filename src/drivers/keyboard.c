#include "keyboard.h"
#include "../io.h"
#include <stdint.h>

int shift_pressed = 0;

static char sc_to_ascii(uint8_t sc){
    if(sc == 0x2A || sc == 0x36){ shift_pressed = 1; return 0; }
    if(sc == 0xAA || sc == 0xB6){ shift_pressed = 0; return 0; }
    if(sc > 0 && sc < 128) return shift_pressed ? scancode_map_shift[sc] : scancode_map[sc];
    return 0;
}

char kb_poll(void) {
    if (inb(kbd_status_port) & 1) {
        uint8_t scancode = inb(kbd_data_port);
        return sc_to_ascii(scancode);
    }
    return 0; // no key pressed
}


char getkey(void){
    while (inb(0x64) & 1) {
        (void)inb(0x60);
    }

    for (;;) {
        if (inb(kbd_status_port) & 1) {
            uint8_t scancode = inb(kbd_data_port);
            return sc_to_ascii(scancode);
        }
    }
}