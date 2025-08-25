#include "idt.h"
#include <stdint.h>

extern void irq0();
extern void irq1();
extern void irq2();
extern void irq3();
extern void irq4();
extern void irq5();
extern void irq6();
extern void irq7();
extern void irq8();
extern void irq9();
extern void irq10();
extern void irq11();
extern void irq12();
extern void irq13();
extern void irq14();
extern void irq15();

static idt_entry_t idt[256];
static idt_ptr_t   idtp;

static inline void lidt(const idt_ptr_t* idt_ptr) {
    __asm__ volatile ("lidt %0" : : "m"(*idt_ptr));
}

static inline uint16_t get_cs(void) {
    uint16_t cs;
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
    return cs;
}

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low = (uint16_t)(base & 0xFFFF);
    idt[num].sel      = sel;
    idt[num].always0  = 0;
    idt[num].flags    = flags;
    idt[num].base_high= (uint16_t)((base >> 16) & 0xFFFF);
}

void idt_install(void) {
    //zero IDT
    for (int i = 0; i < 256; ++i) {
        idt[i].base_low = 0;
        idt[i].sel = 0;
        idt[i].always0 = 0;
        idt[i].flags = 0;
        idt[i].base_high = 0;
    }

    //set IRQ gates 32-47
    uint16_t kcs = get_cs();
    idt_set_gate(32, (uint32_t)irq0,  kcs, 0x8E);
    idt_set_gate(33, (uint32_t)irq1,  kcs, 0x8E);
    idt_set_gate(34, (uint32_t)irq2,  kcs, 0x8E);
    idt_set_gate(35, (uint32_t)irq3,  kcs, 0x8E);
    idt_set_gate(36, (uint32_t)irq4,  kcs, 0x8E);
    idt_set_gate(37, (uint32_t)irq5,  kcs, 0x8E);
    idt_set_gate(38, (uint32_t)irq6,  kcs, 0x8E);
    idt_set_gate(39, (uint32_t)irq7,  kcs, 0x8E);
    idt_set_gate(40, (uint32_t)irq8,  kcs, 0x8E);
    idt_set_gate(41, (uint32_t)irq9,  kcs, 0x8E);
    idt_set_gate(42, (uint32_t)irq10, kcs, 0x8E);
    idt_set_gate(43, (uint32_t)irq11, kcs, 0x8E);
    idt_set_gate(44, (uint32_t)irq12, kcs, 0x8E);
    idt_set_gate(45, (uint32_t)irq13, kcs, 0x8E);
    idt_set_gate(46, (uint32_t)irq14, kcs, 0x8E);
    idt_set_gate(47, (uint32_t)irq15, kcs, 0x8E);

    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (uint32_t)&idt[0];

    lidt(&idtp);
}
