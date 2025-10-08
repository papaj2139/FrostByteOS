#include "kshutdown.h"
#include "../arch/x86/acpi.h"
#include "../io.h"

void kshutdown(void) {
    //try ACPI shutdown
    acpi_shutdown();

    //fallbacks for emulators if ACPI fails/returns
    outw(0x604, 0x2000);  //QEMU
    outw(0xB004, 0x2000); //Bochs
    outb(0xF4, 0x00);     //QEMU isa-debug-exit

    for(;;) { 
        __asm__ volatile ("hlt"); 
    }
}
