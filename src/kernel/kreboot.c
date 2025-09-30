#include "kreboot.h"
#include <stdint.h>
#include "../io.h"

void kreboot(void){
    __asm__ volatile("cli");
    //reset via keyboard controller
    outb(0x64, 0xFE);
    for (volatile unsigned int i = 0; i < 100000; ++i) { }
    //fallback port 0xCF9 (reset control register)
    outb(0xCF9, 0x02); //set reset bit
    for (volatile unsigned int i = 0; i < 100000; ++i) { }
    outb(0xCF9, 0x06); //full reset
    for(;;) { 
        __asm__ volatile("hlt"); 
    }
}
