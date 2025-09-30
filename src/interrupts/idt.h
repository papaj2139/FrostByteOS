#ifndef IDT_H
#define IDT_H

#include <stdint.h>

//32-bit IDT entry
typedef struct __attribute__((packed)) {
    uint16_t base_low;   //lower 16 bits of handler address
    uint16_t sel;        //kernel code segment selector
    uint8_t  always0;
    uint8_t  flags;      //present DPL type (0x8E = present ring0 32-bit interrupt gate)
    uint16_t base_high;  //upper 16 bits of handler address
} idt_entry_t;

//IDT pointer used by lidt
typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint32_t base;
} idt_ptr_t;

void idt_install(void);
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);

#endif
