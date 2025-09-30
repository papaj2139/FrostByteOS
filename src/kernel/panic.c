#include "panic.h"
#include "cga.h"
#include "kreboot.h"
#include "../drivers/pc_speaker.h"
#include "../io.h"
#include <string.h>
 
static const char* g_panic_reason = 0;
 
void kpanic_msg(const char* reason){
    g_panic_reason = reason;
    kpanic();
}
 
void kpanic(void) {
    __asm__ volatile ("cli");
 
    // Fill screen with blue (white on blue)
    cga_clear_with_attr(0x1F);
 
    if(strcmp(bsodVer, "modern") == 0){
        cga_print_at(":(", 0x1F, 0, 0);
        cga_print_at("Your pc ran into a problem and needs to restart.", 0x1F, 0, 1);
        cga_print_at("Please wait while we gather information about this (0%)", 0x1F, 0, 2);
        if (g_panic_reason && g_panic_reason[0]) {
            cga_print_at("Reason:", 0x1F, 0, 3);
            cga_print_at((char*)g_panic_reason, 0x1F, 8, 3);
        } else {
            cga_print_at("Reason: (unspecified)", 0x1F, 0, 3);
        }
    } else{
        cga_print_at(" FrostByte ", 0x71, 35, 4); // Gray background, black text
        if (g_panic_reason && g_panic_reason[0]) {
            cga_print_at("A fatal error has occurred:", 0x1F, 2, 6);
            cga_print_at((char*)g_panic_reason, 0x1F, 2, 7);
        } else {
            cga_print_at("A fatal exception has occurred.", 0x1F, 2, 6);
            cga_print_at("The current application will be terminated.", 0x1F, 2, 7);
        }
        cga_print_at("* Press any key to terminate the current application.", 0x1F, 2, 8);
        cga_print_at("* Press CTRL+ALT+DEL to restart your computer. You will", 0x1F, 2, 9);
        cga_print_at("  lose any unsaved information in all applications.", 0x1F, 2, 10);
        cga_print_at("  Press enter to reboot. ", 0x1F, 25, 15);
        move_cursor(26, 15);
    }
 
    //drain keyboard buffer
    while (inb(0x64) & 1) { (void)inb(0x60); }
 
    ERROR_SOUND();
 
    //wait for enter key
    for (;;) {
        if (inb(0x64) & 1) {
            uint8_t scancode = inb(0x60);
            if (scancode == 0x1C) { //enter
                kreboot();
            }
        }
    }
}
