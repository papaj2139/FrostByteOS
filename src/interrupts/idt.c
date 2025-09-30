#include "idt.h"
#include <stdint.h>

//CPU exceptions (0-31)
extern void isr0();
extern void isr1();
extern void isr2();
extern void isr3();
extern void isr4();
extern void isr5();
extern void isr6();
extern void isr7();
extern void isr8();
extern void isr9();
extern void isr10();
extern void isr11();
extern void isr12();
extern void isr13();
extern void isr14();
extern void isr15();
extern void isr16();
extern void isr17();
extern void isr18();
extern void isr19();
extern void isr20();
extern void isr21();
extern void isr22();
extern void isr23();
extern void isr24();
extern void isr25();
extern void isr26();
extern void isr27();
extern void isr28();
extern void isr29();
extern void isr30();
extern void isr31();

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

    //set exception gates 0-31
    uint16_t kcs = get_cs();
    idt_set_gate(0,  (uint32_t)isr0,  kcs, 0x8E);
    idt_set_gate(1,  (uint32_t)isr1,  kcs, 0x8E);
    idt_set_gate(2,  (uint32_t)isr2,  kcs, 0x8E);
    idt_set_gate(3,  (uint32_t)isr3,  kcs, 0x8E);
    idt_set_gate(4,  (uint32_t)isr4,  kcs, 0x8E);
    idt_set_gate(5,  (uint32_t)isr5,  kcs, 0x8E);
    idt_set_gate(6,  (uint32_t)isr6,  kcs, 0x8E);
    idt_set_gate(7,  (uint32_t)isr7,  kcs, 0x8E);
    idt_set_gate(8,  (uint32_t)isr8,  kcs, 0x8E);
    idt_set_gate(9,  (uint32_t)isr9,  kcs, 0x8E);
    idt_set_gate(10, (uint32_t)isr10, kcs, 0x8E);
    idt_set_gate(11, (uint32_t)isr11, kcs, 0x8E);
    idt_set_gate(12, (uint32_t)isr12, kcs, 0x8E);
    idt_set_gate(13, (uint32_t)isr13, kcs, 0x8E);
    idt_set_gate(14, (uint32_t)isr14, kcs, 0x8E);
    idt_set_gate(15, (uint32_t)isr15, kcs, 0x8E);
    idt_set_gate(16, (uint32_t)isr16, kcs, 0x8E);
    idt_set_gate(17, (uint32_t)isr17, kcs, 0x8E);
    idt_set_gate(18, (uint32_t)isr18, kcs, 0x8E);
    idt_set_gate(19, (uint32_t)isr19, kcs, 0x8E);
    idt_set_gate(20, (uint32_t)isr20, kcs, 0x8E);
    idt_set_gate(21, (uint32_t)isr21, kcs, 0x8E);
    idt_set_gate(22, (uint32_t)isr22, kcs, 0x8E);
    idt_set_gate(23, (uint32_t)isr23, kcs, 0x8E);
    idt_set_gate(24, (uint32_t)isr24, kcs, 0x8E);
    idt_set_gate(25, (uint32_t)isr25, kcs, 0x8E);
    idt_set_gate(26, (uint32_t)isr26, kcs, 0x8E);
    idt_set_gate(27, (uint32_t)isr27, kcs, 0x8E);
    idt_set_gate(28, (uint32_t)isr28, kcs, 0x8E);
    idt_set_gate(29, (uint32_t)isr29, kcs, 0x8E);
    idt_set_gate(30, (uint32_t)isr30, kcs, 0x8E);
    idt_set_gate(31, (uint32_t)isr31, kcs, 0x8E);

    //set IRQ gates 32-47
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
