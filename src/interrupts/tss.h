#ifndef TSS_H
#define TSS_H

#include <stdint.h>

//TSS structure for i386
typedef struct __attribute__((packed)) {
    uint32_t prev_tss;   //previous TSS - unused in software task switching
    uint32_t esp0;       //kernel stack pointer
    uint32_t ss0;        //kernel stack segment
    uint32_t esp1;       //unused
    uint32_t ss1;        //unused
    uint32_t esp2;       //unused
    uint32_t ss2;        //unused
    uint32_t cr3;        //page directory base
    uint32_t eip;        //instruction pointer
    uint32_t eflags;     //flags register
    uint32_t eax, ecx, edx, ebx; //general purpose registers
    uint32_t esp, ebp, esi, edi; //general purpose registers
    uint32_t es, cs, ss, ds, fs, gs; //segment registers
    uint32_t ldt;        //LDT selector - unused
    uint16_t trap;       //trap on task switch
    uint16_t iomap_base; //I/O map base address
} tss_t;

//function declarations
void tss_init(void);
void tss_set_kernel_stack(uint32_t stack);

#endif
