#include "tss.h"
#include "gdt.h"
#include <stdint.h>
#include <string.h>

//global TSS
static tss_t kernel_tss;

//kernel stack for TSS (increase size to tolerate deep syscall call chains)
static uint8_t kernel_stack[16384] __attribute__((aligned(16)));

void tss_init(void) {
    //clear TSS structure
    memset(&kernel_tss, 0, sizeof(tss_t));
    
    //set up kernel stack
    kernel_tss.ss0 = 0x10;  //kernel data segment
    kernel_tss.esp0 = (uint32_t)(kernel_stack + sizeof(kernel_stack)); //top of stack
    
    //set I/O map base to end of TSS (no I/O bitmap)
    kernel_tss.iomap_base = sizeof(tss_t);
    
    //add TSS descriptor to GDT
    //need to add this at GDT entry 5 (0x28)
    //TSS descriptor format: base, limit, access, flags
    uint32_t tss_base = (uint32_t)&kernel_tss;
    uint32_t tss_limit = sizeof(tss_t) - 1;
    
    //install TSS descriptor in GDT
    gdt_set_gate(5, tss_base, tss_limit, 0x89, 0x00); //present ring 0 TSS
    
    //load TSS
    __asm__ volatile ("ltr %%ax" : : "a" (0x28)); //load TSS selector 0x28
    }

void tss_set_kernel_stack(uint32_t stack) {
    kernel_tss.esp0 = stack;
}
